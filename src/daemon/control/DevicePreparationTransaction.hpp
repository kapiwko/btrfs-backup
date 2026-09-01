// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <daemon/control/DeviceProvisioningService.hpp>

namespace btrfsbackup::daemon::control {

struct DevicePreparationTransaction {
    DevicePreparationStatus status;
    DevicePreparationOwner owner;
    ProvisioningDevice device;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
    std::string last_completed_phase;
    std::string partition;
    std::string partition_uuid;
    std::string luks_uuid;
    std::string btrfs_uuid;
    std::string mapper;
    std::string configuration_state = "not-started";
    std::string credentials_state = "not-started";
    std::string cleanup_result = "not-required";
};

class DevicePreparationTransactionStore final {
  public:
    DevicePreparationTransactionStore(
        std::filesystem::path root,
        std::size_t completed_limit = 128,
        std::chrono::seconds completed_ttl = std::chrono::hours(24 * 30)
    );

    void save(const DevicePreparationTransaction& transaction) const;
    [[nodiscard]] std::vector<DevicePreparationTransaction> load_and_prune() const;

  private:
    std::filesystem::path root_;
    std::size_t completed_limit_;
    std::chrono::seconds completed_ttl_;
};

} // namespace btrfsbackup::daemon::control
