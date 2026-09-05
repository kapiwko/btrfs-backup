// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace btrfsbackup::platform::linux::config {

struct ProfileMigrationIssue {
    std::string profile_id;
    std::string message;
};

struct ProfileMigrationPreflight {
    std::vector<std::string> ready_profiles;
    std::vector<ProfileMigrationIssue> issues;
};

ProfileMigrationPreflight inspect_profile_migration_readiness(const std::filesystem::path& etc_root);
std::vector<std::string> export_all_profiles_v4(
    const std::filesystem::path& etc_root,
    const std::filesystem::path& output_dir
);

} // namespace btrfsbackup::platform::linux::config
