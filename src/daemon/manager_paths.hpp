// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

namespace btrfsbackup::daemon {

struct ManagerPaths {
    std::filesystem::path config_root = "/etc/btrfs-backup";
    std::filesystem::path public_profile_root = "/var/lib/btrfs-backup/public/profiles";
    std::filesystem::path status_root = "/run/btrfs-backup/profiles";
    std::filesystem::path history_root = "/var/lib/btrfs-backup/history";
    std::filesystem::path target_mount_root = "/mnt/btrfs-backup";
    std::filesystem::path mapper_root = "/dev/mapper";
    std::filesystem::path mountinfo_path = "/proc/self/mountinfo";
};

} // namespace btrfsbackup::daemon
