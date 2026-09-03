// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <config/domain/Profile.hpp>
#include <provisioning/DevicePreparationTransaction.hpp>

namespace btrfsbackup::daemon::control {

class ProvisionedProfileBuilder final {
  public:
    explicit ProvisionedProfileBuilder(std::filesystem::path target_mount_root);

    [[nodiscard]] config::Profile build(
        const provisioning::DevicePreparationTransaction& transaction,
        const std::string& luks_uuid,
        const std::string& btrfs_uuid,
        const std::string& partition_uuid
    ) const;

  private:
    std::filesystem::path target_mount_root_;
};

} // namespace btrfsbackup::daemon::control
