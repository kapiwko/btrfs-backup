// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include <config/model/json.hpp>

namespace btrfsbackup {

struct ManagerPaths {
    std::filesystem::path config_root = "/etc/btrfs-backup";
    std::filesystem::path public_profile_root = "/var/lib/btrfs-backup/public/profiles";
    std::filesystem::path status_root = "/run/btrfs-backup/profiles";
    std::filesystem::path history_root = "/var/lib/btrfs-backup/history";
    std::filesystem::path target_mount_root = "/mnt/btrfs-backup";
    std::filesystem::path mapper_root = "/dev/mapper";
    std::filesystem::path mountinfo_path = "/proc/self/mountinfo";
};

class ManagerService {
  public:
    explicit ManagerService(ManagerPaths paths);

    [[nodiscard]] Json get_capabilities() const;
    [[nodiscard]] Json list_profiles() const;
    [[nodiscard]] Json get_status(const std::string& profile_id) const;
    [[nodiscard]] Json get_history_sanitized(
        const std::string& profile_id,
        std::size_t offset,
        std::size_t limit
    ) const;
    [[nodiscard]] Json get_device_state(const std::string& profile_id) const;

  private:
    ManagerPaths paths_;
};

} // namespace btrfsbackup
