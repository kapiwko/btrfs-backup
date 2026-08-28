// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/profile_artifact_renderer.hpp>

#include <string>
#include <utility>

#include <config/model/json.hpp>
#include <config/model/json_io.hpp>
#include <config/model/profile_document.hpp>
#include <config/profile_render.hpp>
#include <core/durable_file_operations.hpp>
#include <core/errors.hpp>

namespace btrfsbackup::config {

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
                .permissions = public_read_file_permissions,
            },
            {
                .kind = ProfileArtifactKind::SystemdMountDependency,
                .destination = roots.systemd_root / ("btrfs-backup@" + profile_id + ".service.d") / "target-mount.conf",
                .content = render_mount_dependency(rendered),
                .permissions = public_read_file_permissions,
            },
            {
                .kind = ProfileArtifactKind::PrivateProfile,
                .destination = roots.etc_root / "profiles" / profile_id / "profile.json",
                .content = dump_json(profile_to_json(rendered)),
                .permissions = private_file_permissions,
            },
            {
                .kind = ProfileArtifactKind::PublicProfile,
                .destination = roots.public_root / (profile_id + ".json"),
                .content = dump_json(public_profile_json(rendered)),
                .permissions = public_read_file_permissions,
            },
        },
    };
}

ProfileArtifactRoots profile_artifact_roots(const std::filesystem::path& output_dir) {
    return {
        .etc_root = output_dir / "etc" / "btrfs-backup",
        .udev_root = output_dir / "etc" / "udev" / "rules.d",
        .systemd_root = output_dir / "etc" / "systemd" / "system",
        .public_root = output_dir / "var" / "lib" / "btrfs-backup" / "public" / "profiles",
    };
}

} // namespace btrfsbackup::config
