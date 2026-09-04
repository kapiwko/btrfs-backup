// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace btrfsbackup::integration {

class RealTrustedHookTest final {
  public:
    RealTrustedHookTest(
        std::filesystem::path runtime,
        std::filesystem::path profile,
        std::filesystem::path test_root
    );
    ~RealTrustedHookTest() noexcept;

    RealTrustedHookTest(const RealTrustedHookTest&) = delete;
    RealTrustedHookTest& operator=(const RealTrustedHookTest&) = delete;

    void run();
    void close();

  private:
    void configure_hook();
    void require_success(std::vector<std::string> arguments, std::string_view operation) const;
    void expect_backup_failure(std::string_view expected) const;
    [[nodiscard]] std::vector<std::string> release_resources() noexcept;

    std::filesystem::path runtime_;
    std::filesystem::path profile_;
    std::filesystem::path hook_directory_{"/etc/btrfs-backup/hooks.d"};
    std::filesystem::path hook_;
    std::filesystem::path original_hook_;
    std::filesystem::path outside_hook_;
    std::filesystem::path marker_;
    std::string original_profile_;
    bool profile_modified_{false};
    bool hook_directory_permissions_modified_{false};
    bool closed_{false};
};

} // namespace btrfsbackup::integration
