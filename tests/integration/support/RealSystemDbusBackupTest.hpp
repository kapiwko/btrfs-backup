// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace btrfsbackup::integration {

class RealSystemDbusBackupTest final {
  public:
    RealSystemDbusBackupTest(
        std::filesystem::path source_mount,
        std::filesystem::path target_mount,
        std::filesystem::path test_root,
        std::filesystem::path client
    );
    ~RealSystemDbusBackupTest() noexcept;

    RealSystemDbusBackupTest(const RealSystemDbusBackupTest&) = delete;
    RealSystemDbusBackupTest& operator=(const RealSystemDbusBackupTest&) = delete;

    void run();
    void close();

  private:
    void require_policy(std::string_view action) const;
    [[nodiscard]] std::string call_as_test_user(std::string_view method) const;
    void wait_for_backup(std::string_view operation_id) const;
    void verify_backup() const;
    [[nodiscard]] std::vector<std::string> release_resources() noexcept;

    std::filesystem::path source_mount_;
    std::filesystem::path target_mount_;
    std::filesystem::path test_root_;
    std::filesystem::path client_;
    std::filesystem::path policy_rule_{"/etc/polkit-1/rules.d/49-btrfs-backup-integration.rules"};
    bool user_created_{false};
    bool services_started_{false};
    bool closed_{false};
};

} // namespace btrfsbackup::integration
