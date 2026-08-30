// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <config/domain/Profile.hpp>
#include <config/ports/ConfigurationActivator.hpp>

namespace btrfsbackup::platform::linux {

// File-oriented profile operations exposed to Linux frontends.

struct ProfileInstallationRoots {
    std::filesystem::path etc_root;
    std::filesystem::path udev_root;
    std::filesystem::path systemd_root;
    std::filesystem::path public_root;
};

btrfsbackup::config::Profile validate_profile_file(
    const std::filesystem::path& file,
    const std::filesystem::path& target_mount_root = "/mnt/btrfs-backup"
);
void write_profile_file(const btrfsbackup::config::Profile& profile, const std::filesystem::path& output);
void render_profile(
    const std::filesystem::path& file,
    const std::filesystem::path& output_dir,
    const std::filesystem::path& target_mount_root = "/mnt/btrfs-backup"
);
btrfsbackup::config::Profile save_profile(
    const std::filesystem::path& file,
    const ProfileInstallationRoots& roots,
    btrfsbackup::config::IConfigurationActivator& activator
);
void install_profile(
    const btrfsbackup::config::Profile& profile,
    const ProfileInstallationRoots& roots,
    btrfsbackup::config::IConfigurationActivator& activator
);
btrfsbackup::config::Profile get_profile(const std::filesystem::path& etc_root, const std::string& profile_id);
btrfsbackup::config::Profile export_profile(
    const std::filesystem::path& etc_root,
    const std::string& profile_id,
    const std::filesystem::path& output
);
std::vector<std::string> list_profiles(const std::filesystem::path& profile_root);

} // namespace btrfsbackup::platform::linux
