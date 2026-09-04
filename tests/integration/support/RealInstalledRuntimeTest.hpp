// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace btrfsbackup::integration {

class RealInstalledRuntimeTest final {
  public:
    RealInstalledRuntimeTest(
        std::filesystem::path backupctl,
        std::filesystem::path runtime,
        std::filesystem::path test_root,
        std::filesystem::path source_mount,
        std::filesystem::path target_mount,
        std::filesystem::path target_device,
        std::string mapper_name,
        std::filesystem::path passphrase_file
    );

    void configure_and_install() const;
    void activate_managed_target() const;
    void validate_runtime() const;

  private:
    [[nodiscard]] std::string mount_unit() const;
    void require_success(std::vector<std::string> arguments, std::string_view operation) const;

    std::filesystem::path backupctl_;
    std::filesystem::path runtime_;
    std::filesystem::path rendered_root_;
    std::filesystem::path source_mount_;
    std::filesystem::path target_mount_;
    std::filesystem::path target_device_;
    std::string mapper_name_;
    std::filesystem::path mapper_path_;
    std::filesystem::path passphrase_file_;
};

} // namespace btrfsbackup::integration
