// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace btrfsbackup::integration {

class RealSandboxedSystemdTest final {
  public:
    RealSandboxedSystemdTest(
        std::filesystem::path source_mount,
        std::filesystem::path target_mount,
        std::filesystem::path staging_mount,
        std::string mapper_name,
        std::filesystem::path profile
    );
    ~RealSandboxedSystemdTest() noexcept;

    RealSandboxedSystemdTest(const RealSandboxedSystemdTest&) = delete;
    RealSandboxedSystemdTest& operator=(const RealSandboxedSystemdTest&) = delete;

    void run_sandboxed_backup();
    void run_automatic_eject();
    void close();

  private:
    [[nodiscard]] std::string mount_unit() const;
    [[nodiscard]] std::string property(std::string_view unit, std::string_view name) const;
    [[nodiscard]] std::size_t eject_completion_count() const;
    void wait_for_eject_service(std::size_t previous_count) const;
    void require_success(std::vector<std::string> arguments, std::string_view operation) const;
    void require_latest_snapshots_match() const;
    [[nodiscard]] std::vector<std::string> release_resources() noexcept;

    std::filesystem::path source_mount_;
    std::filesystem::path target_mount_;
    std::filesystem::path staging_mount_;
    std::string mapper_name_;
    std::filesystem::path mapper_path_;
    std::filesystem::path profile_;
    std::filesystem::path mount_unit_path_;
    std::filesystem::path mount_dependency_path_{
        "/etc/systemd/system/btrfs-backup@default.service.d/target-mount.conf"
    };
    std::string original_profile_;
    std::string original_mount_unit_;
    std::string original_mount_dependency_;
    bool system_files_modified_{false};
    bool closed_{false};
};

} // namespace btrfsbackup::integration
