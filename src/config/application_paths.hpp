// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup::config {

struct ApplicationPaths {
    std::filesystem::path state_root;
    std::filesystem::path status_root;
    std::filesystem::path history_root;
    std::filesystem::path target_mount_root;
};

inline std::filesystem::path profile_state_dir(const ApplicationPaths& paths, const std::string& profile_id) {
    return paths.state_root / "profiles" / profile_id;
}

inline std::filesystem::path profile_mount_point(const ApplicationPaths& paths, const std::string& profile_id) {
    return paths.target_mount_root / profile_id;
}

} // namespace btrfsbackup::config
