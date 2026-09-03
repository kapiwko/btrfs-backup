// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <daemon/control/DevicePreparationTransaction.hpp>

namespace btrfsbackup::daemon::control {

using DevicePreparationTransition = std::function<void(DevicePreparationTransaction&)>;

struct DevicePreparationTransactionScan {
    std::vector<DevicePreparationTransaction> transactions;
    std::vector<std::string> corrupted_operation_ids;
};

class DevicePreparationTransactionStore final {
  public:
    DevicePreparationTransactionStore(
        std::filesystem::path root,
        std::size_t completed_limit = 128,
        std::chrono::seconds completed_ttl = std::chrono::hours(24 * 30)
    );

    void save(DevicePreparationTransaction& transaction) const;
    [[nodiscard]] DevicePreparationTransaction update(
        const std::string& operation_id,
        TransactionRevision expected_revision,
        const DevicePreparationTransition& transition
    ) const;
    [[nodiscard]] DevicePreparationTransaction update(
        const std::string& operation_id,
        const DevicePreparationTransition& transition
    ) const;
    [[nodiscard]] DevicePreparationTransaction load(const std::string& operation_id) const;
    [[nodiscard]] DevicePreparationTransactionScan load_and_prune() const;
    void reserve_profile(const std::string& profile_id, const std::string& operation_id) const;
    void release_profile(const std::string& profile_id, const std::string& operation_id) const;
    [[nodiscard]] std::optional<std::string> profile_reservation_owner(const std::string& profile_id) const;

  private:
    std::filesystem::path root_;
    std::size_t completed_limit_;
    std::chrono::seconds completed_ttl_;
};

} // namespace btrfsbackup::daemon::control
