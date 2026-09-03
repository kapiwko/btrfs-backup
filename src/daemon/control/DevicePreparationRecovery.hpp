// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <daemon/control/DevicePreparationTransactionStore.hpp>

namespace btrfsbackup::platform::linux::storage {
class ICryptsetupOperations;
class IPartitionTableOperations;
} // namespace btrfsbackup::platform::linux::storage

namespace btrfsbackup::daemon::control {

class IExistingTargetInspector;

class DevicePreparationRecovery final {
  public:
    DevicePreparationRecovery(
        DevicePreparationTransactionStore& transactions,
        platform::linux::storage::IPartitionTableOperations& partition_tables,
        platform::linux::storage::ICryptsetupOperations& cryptsetup,
        IExistingTargetInspector* existing_target_inspector
    );

    void recover(const std::string& operation_id);

  private:
    void persist(DevicePreparationTransaction& transaction) const;
    [[nodiscard]] bool inspect_replaced_partition(DevicePreparationTransaction& transaction) const;
    [[nodiscard]] bool inspect_created_partition(DevicePreparationTransaction& transaction) const;
    void cleanup_existing_target(DevicePreparationTransaction& transaction) const;
    void close_mapper(DevicePreparationTransaction& transaction) const;

    DevicePreparationTransactionStore& transactions_;
    platform::linux::storage::IPartitionTableOperations& partition_tables_;
    platform::linux::storage::ICryptsetupOperations& cryptsetup_;
    IExistingTargetInspector* existing_target_inspector_;
};

} // namespace btrfsbackup::daemon::control
