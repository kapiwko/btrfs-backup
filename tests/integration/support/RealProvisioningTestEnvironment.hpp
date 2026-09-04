// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "IntegrationTestProcess.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace btrfsbackup::integration {

class RealProvisioningTestEnvironment final {
  public:
    RealProvisioningTestEnvironment(
        std::filesystem::path client,
        std::filesystem::path source
    );
    ~RealProvisioningTestEnvironment() noexcept;

    RealProvisioningTestEnvironment(const RealProvisioningTestEnvironment&) = delete;
    RealProvisioningTestEnvironment& operator=(const RealProvisioningTestEnvironment&) = delete;

    void require_existing_partition_preserves_sibling();
    void close();

  private:
    [[nodiscard]] CommandResult command(
        std::vector<std::string> arguments,
        std::string_view standard_input = {}
    ) const;
    void require_command(std::vector<std::string> arguments, std::string_view operation) const;
    void attach_image(std::string_view size);
    void start_manager();
    void stop_manager();
    [[nodiscard]] std::string provision(
        const std::filesystem::path& target,
        std::string_view mode,
        std::string_view profile_id
    ) const;
    void delete_profile(std::string_view profile_id) const;
    [[nodiscard]] std::vector<std::string> release_resources() noexcept;

    std::filesystem::path client_;
    std::filesystem::path source_;
    std::filesystem::path root_;
    std::filesystem::path image_;
    std::filesystem::path preserved_mount_;
    std::string loop_;
    std::string passphrase_{"btrfs-backup-provisioning-test-passphrase\n"};
    bool source_bind_mounted_{false};
    bool preserved_mounted_{false};
    bool manager_started_{false};
    bool closed_{false};
};

} // namespace btrfsbackup::integration
