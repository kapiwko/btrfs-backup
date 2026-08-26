// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/profile_artifact_renderer.hpp>

#include <sys/random.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <string>
#include <utility>

#include <config/errors.hpp>
#include <config/json.hpp>
#include <config/json_io.hpp>
#include <config/profile_render.hpp>
#include <platform/linux/file_io.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

Json public_profile_json(const Profile& profile) {
    Json sources = Json::array();
    for (const ProfileSource& source : profile.sources) {
        sources.push_back({{"id", std::string(source.id.value())}, {"name", source.name}});
    }
    Json result = {
        {"schemaVersion", 1},
        {"profileId", std::string(profile.id.value())},
        {"name", profile.name},
        {"target", {{"name", profile.target.mapper_name}}},
        {"sources", std::move(sources)},
    };
    if (!profile.configuration_generation.empty()) {
        result["configurationGeneration"] = profile.configuration_generation;
    }
    return result;
}

} // namespace

ProfileArtifactRenderer::ProfileArtifactRenderer(ConfigurationGenerationGenerator generations)
    : generations_(std::move(generations)) {
    if (!generations_) {
        throw ValidationError("configuration generation generator is required");
    }
}

RenderedProfileArtifacts ProfileArtifactRenderer::render_profile_artifacts(
    const Profile& profile,
    const ProfileArtifactRoots& roots
) const {
    Profile rendered = profile;
    rendered.configuration_generation = generations_();
    if (rendered.configuration_generation.empty()) {
        throw ValidationError("configuration generation must not be empty");
    }
    const std::string profile_id{rendered.id.value()};
    return {
        .profile = rendered,
        .artifacts = {
            {
                .kind = ProfileArtifactKind::UdevRule,
                .destination = roots.udev_root / ("99-btrfs-backup-" + profile_id + ".rules"),
                .content = render_udev(rendered),
                .mode = 0644,
            },
            {
                .kind = ProfileArtifactKind::SystemdMountDependency,
                .destination = roots.systemd_root / ("btrfs-backup@" + profile_id + ".service.d")
                    / "target-mount.conf",
                .content = render_mount_dependency(rendered),
                .mode = 0644,
            },
            {
                .kind = ProfileArtifactKind::PrivateProfile,
                .destination = roots.etc_root / "profiles" / profile_id / "profile.json",
                .content = dump_json(profile_to_json(rendered)),
                .mode = 0600,
            },
            {
                .kind = ProfileArtifactKind::PublicProfile,
                .destination = roots.public_root / (profile_id + ".json"),
                .content = dump_json(public_profile_json(rendered)),
                .mode = 0644,
            },
        },
    };
}

std::string generate_configuration_generation() {
    std::array<unsigned char, 16> bytes{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw ValidationError("cannot generate configuration generation");
        }
        offset += static_cast<std::size_t>(count);
    }

    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

ProfileArtifactRoots profile_artifact_roots(const fs::path& output_dir) {
    return {
        .etc_root = output_dir / "etc" / "btrfs-backup",
        .udev_root = output_dir / "etc" / "udev" / "rules.d",
        .systemd_root = output_dir / "etc" / "systemd" / "system",
        .public_root = output_dir / "var" / "lib" / "btrfs-backup" / "public" / "profiles",
    };
}

void write_profile_artifacts(const RenderedProfileArtifacts& rendered) {
    for (const ProfileArtifact& artifact : rendered.artifacts) {
        atomic_write(artifact.destination, artifact.content, artifact.mode);
    }
}

} // namespace btrfsbackup
