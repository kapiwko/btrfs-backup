// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/ProfileArtifactRenderer.hpp>

#include <string>
#include <utility>

#include <config/model/Json.hpp>
#include <config/model/JsonIo.hpp>
#include <config/model/ProfileDocument.hpp>
#include <config/ProfileRender.hpp>
#include <core/Errors.hpp>

namespace btrfsbackup::config {

namespace {

constexpr std::filesystem::perms private_profile_permissions =
    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
constexpr std::filesystem::perms public_artifact_permissions =
    private_profile_permissions | std::filesystem::perms::group_read | std::filesystem::perms::others_read;

Json public_profile_json(const Profile& profile) {
    Json sources = Json::array();
    for (const ProfileSource& source : profile.sources) {
        sources.push_back({{"id", std::string(source.id.value())}, {"name", source.name}});
    }
    Json result = {
        {"schemaVersion", 1},
        {"profileId", std::string(profile.id.value())},
        {"name", profile.name},
        {"target", {{"name", profile.target.mapper_name.value()}}},
        {"sources", std::move(sources)},
    };
    if (!profile.configuration_generation.empty()) {
        result["configurationGeneration"] = profile.configuration_generation.value();
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
    const std::string mount_unit = target_mount_unit_name(rendered.target.mount_point);
    return {
        .profile = rendered,
        .artifacts = {
            {
                .kind = ProfileArtifactKind::UdevRule,
                .destination = roots.udev_root / ("99-btrfs-backup-" + profile_id + ".rules"),
                .content = render_udev(rendered),
                .permissions = public_artifact_permissions,
            },
            {
                .kind = ProfileArtifactKind::SystemdMountDependency,
                .destination = roots.systemd_root / ("btrfs-backup@" + profile_id + ".service.d") / "target-mount.conf",
                .content = render_mount_dependency(rendered),
                .permissions = public_artifact_permissions,
            },
            {
                .kind = ProfileArtifactKind::NativeTargetMount,
                .destination = roots.systemd_root / mount_unit,
                .content = render_target_mount_unit(rendered),
                .permissions = public_artifact_permissions,
            },
            {
                .kind = ProfileArtifactKind::PrivateProfile,
                .destination = roots.etc_root / "profiles" / profile_id / "profile.json",
                .content = dump_json(profile_to_json(rendered)),
                .permissions = private_profile_permissions,
            },
            {
                .kind = ProfileArtifactKind::ManagedArtifactManifest,
                .destination = roots.etc_root / "profiles" / profile_id / "managed-artifacts.json",
                .content = dump_json({
                    {"schemaVersion", 1},
                    {"profileId", profile_id},
                    {"mounts", Json::array({{
                                   {"unit", mount_unit},
                                   {"mountPoint", rendered.target.mount_point.value().string()},
                               }})},
                }),
                .permissions = private_profile_permissions,
            },
            {
                .kind = ProfileArtifactKind::PublicProfile,
                .destination = roots.public_root / (profile_id + ".json"),
                .content = dump_json(public_profile_json(rendered)),
                .permissions = public_artifact_permissions,
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
