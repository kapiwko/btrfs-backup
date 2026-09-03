// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstddef>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <daemon/control/DeviceProvisioningService.hpp>

namespace btrfsbackup::daemon::control {

struct TransactionRevision {
    std::uint64_t value = 0;

    auto operator<=>(const TransactionRevision&) const = default;
};

struct DevicePreparationTransaction {
    TransactionRevision revision;
    DevicePreparationStatus status;
    DevicePreparationOwner owner;
    ProvisioningDevice device;
    DevicePreparationTarget target;
    std::string profile_name;
    std::string source_subvolume;
    std::string source_filesystem_uuid;
    std::string source_mount_root;
    std::string local_snapshot_dir;
    std::string passphrase_label;
    bool create_automatic_key = true;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
    std::string last_completed_phase;
    std::string partition_table_backup;
    std::string partition;
    std::string partition_uuid;
    std::string luks_uuid;
    std::string btrfs_uuid;
    std::string mapper;
    std::string inspection_mount_point;
    std::string configuration_state = "not-started";
    std::string credentials_state = "not-started";
    std::string profile_reservation_state = "not-held";
    std::string cleanup_result = "not-required";
    bool cancel_requested = false;
};

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
