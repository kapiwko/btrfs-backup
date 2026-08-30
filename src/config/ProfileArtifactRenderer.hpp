// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <config/domain/Profile.hpp>

namespace btrfsbackup::config {

enum class ProfileArtifactKind {
    UdevRule,
    SystemdMountDependency,
    NativeTargetMount,
    ManagedArtifactManifest,
    PrivateProfile,
    PublicProfile,
    ObsoleteSystemdUnit,
};

enum class ProfileArtifactOperation {
    Write,
    Remove,
};

struct ProfileArtifactRoots {
    std::filesystem::path etc_root;
    std::filesystem::path udev_root;
    std::filesystem::path systemd_root;
    std::filesystem::path public_root;
};

struct ProfileArtifact {
    ProfileArtifactKind kind;
    std::filesystem::path destination;
    std::string content;
    std::filesystem::perms permissions;
    ProfileArtifactOperation operation = ProfileArtifactOperation::Write;
};

struct RenderedProfileArtifacts {
    Profile profile;
    std::vector<ProfileArtifact> artifacts;
};

using ConfigurationGenerationGenerator = std::function<ConfigurationGeneration()>;

class ProfileArtifactRenderer {
  public:
    explicit ProfileArtifactRenderer(ConfigurationGenerationGenerator generations);

    [[nodiscard]] RenderedProfileArtifacts render_profile_artifacts(
        const Profile& profile,
        const ProfileArtifactRoots& roots
    ) const;

  private:
    ConfigurationGenerationGenerator generations_;
};

ProfileArtifactRoots profile_artifact_roots(const std::filesystem::path& output_dir);

} // namespace btrfsbackup::config
