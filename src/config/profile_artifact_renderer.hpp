// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <sys/types.h>
#include <vector>

#include <config/profile.hpp>

namespace btrfsbackup {

enum class ProfileArtifactKind {
    UdevRule,
    SystemdMountDependency,
    PrivateProfile,
    PublicProfile,
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
    mode_t mode;
};

struct RenderedProfileArtifacts {
    Profile profile;
    std::vector<ProfileArtifact> artifacts;
};

using ConfigurationGenerationGenerator = std::function<std::string()>;

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

std::string generate_configuration_generation();
ProfileArtifactRoots profile_artifact_roots(const std::filesystem::path& output_dir);
void write_profile_artifacts(const RenderedProfileArtifacts& rendered);

} // namespace btrfsbackup
