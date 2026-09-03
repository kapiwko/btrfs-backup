// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DevicePreparationRecovery.hpp>

#include <chrono>
#include <filesystem>

#include <core/Errors.hpp>
#include <daemon/control/ExistingTargetInspector.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>
#include <platform/linux/storage/PartitionTableOperations.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

std::int64_t system_time_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    )
        .count();
}

} // namespace

DevicePreparationRecovery::DevicePreparationRecovery(
    DevicePreparationTransactionStore& transactions,
    platform::linux::storage::IPartitionTableOperations& partition_tables,
    platform::linux::storage::ICryptsetupOperations& cryptsetup,
    IExistingTargetInspector* existing_target_inspector
)
    : transactions_(transactions),
      partition_tables_(partition_tables),
      cryptsetup_(cryptsetup),
      existing_target_inspector_(existing_target_inspector) {
}

void DevicePreparationRecovery::recover(const std::string& operation_id) {
    DevicePreparationTransaction transaction = transactions_.load(operation_id);
    if (transaction.status.state != "interrupted")
        throw ValidationError("device preparation transaction does not require cleanup");
    if (inspect_replaced_partition(transaction) || inspect_created_partition(transaction))
        return;
    if (transaction.mapper.empty() && transaction.inspection_mount_point.empty())
        return;
    if (transaction.target.mode == provisioning::ProvisioningMode::AdoptExistingTarget)
        cleanup_existing_target(transaction);
    else
        close_mapper(transaction);
}

void DevicePreparationRecovery::persist(DevicePreparationTransaction& transaction) const {
    transaction.updated_at = system_time_seconds();
    transactions_.save(transaction);
}

bool DevicePreparationRecovery::inspect_replaced_partition(
    DevicePreparationTransaction& transaction
) const {
    if (transaction.target.mode != provisioning::ProvisioningMode::EraseWholeDevice ||
        !transaction.partition.empty() || transaction.last_completed_phase != "wipe-signatures" ||
        transaction.partition_table_backup.empty())
        return false;
    try {
        const auto& planned = *transaction.target.planned_partition_geometry;
        const auto inspection = partition_tables_.inspect_single_gpt_partition(
            transaction.device.path,
            transaction.device.major_minor,
            transaction.target.device.logical_sector_size,
            {
                .start_sector = planned.start_sector,
                .sector_count = planned.sector_count,
                .partition_number = planned.partition_number,
            }
        );
        if (inspection.state == platform::linux::storage::PartitionCreationState::Created) {
            transaction.partition = inspection.partition.string();
            transaction.last_completed_phase = "partition";
            transaction.cleanup_result = "partition-detected";
            transaction.status.recovery_action =
                "The planned replacement GPT and partition exist. Inspect them before continuing manually.";
        } else {
            transaction.cleanup_result = "partition-state-conflict";
            transaction.status.recovery_action =
                "The replacement GPT does not exactly match the saved plan. Inspect the saved partition table backup manually.";
        }
    } catch (...) {
        transaction.cleanup_result = "partition-inspection-failed";
        transaction.status.recovery_action =
            "The replacement GPT could not be verified. Inspect the saved partition table backup manually.";
    }
    persist(transaction);
    return true;
}

bool DevicePreparationRecovery::inspect_created_partition(
    DevicePreparationTransaction& transaction
) const {
    if (transaction.target.mode != provisioning::ProvisioningMode::CreatePartitionInUnallocatedSpace ||
        !transaction.partition.empty() || transaction.last_completed_phase != "backup-partition-table" ||
        transaction.partition_table_backup.empty())
        return false;
    try {
        const auto& free_region = *transaction.target.free_region;
        const auto& planned = *transaction.target.planned_partition_geometry;
        const auto inspection = partition_tables_.inspect_partition_creation(
            transaction.device.path,
            transaction.device.major_minor,
            transaction.target.device.partition_table.identifier,
            transaction.target.device.logical_sector_size,
            free_region.start_sector,
            free_region.sector_count,
            {
                .start_sector = planned.start_sector,
                .sector_count = planned.sector_count,
                .partition_number = planned.partition_number,
            }
        );
        if (inspection.state == platform::linux::storage::PartitionCreationState::Created) {
            transaction.partition = inspection.partition.string();
            transaction.last_completed_phase = "partition";
            transaction.cleanup_result = "partition-detected";
            transaction.status.recovery_action =
                "The planned partition exists. Inspect it before completing or removing it manually.";
        } else if (inspection.state == platform::linux::storage::PartitionCreationState::NotCreated) {
            transaction.cleanup_result = "partition-not-created";
            transaction.status.recovery_action =
                "No new partition was detected. Rescan storage and build a new preparation plan.";
        } else {
            transaction.cleanup_result = "partition-state-conflict";
            transaction.status.recovery_action =
                "The partition table no longer matches the saved plan. Inspect it manually.";
        }
    } catch (...) {
        transaction.cleanup_result = "partition-inspection-failed";
        transaction.status.recovery_action =
            "The partition state could not be verified. Inspect the saved partition table backup manually.";
    }
    persist(transaction);
    return true;
}

void DevicePreparationRecovery::cleanup_existing_target(
    DevicePreparationTransaction& transaction
) const {
    if (existing_target_inspector_ == nullptr || transaction.mapper.empty() ||
        transaction.inspection_mount_point.empty())
        throw ValidationError("existing target cleanup state is incomplete");
    try {
        existing_target_inspector_->cleanup_session(
            transaction.mapper,
            transaction.inspection_mount_point
        );
        std::error_code error;
        static_cast<void>(fs::remove(transaction.inspection_mount_point, error));
        transaction.cleanup_result = error ? "inspection-cleanup-failed" : "inspection-cleaned";
        if (!error) {
            transaction.mapper.clear();
            transaction.inspection_mount_point.clear();
        }
    } catch (...) {
        transaction.cleanup_result = "inspection-cleanup-failed";
    }
    persist(transaction);
}

void DevicePreparationRecovery::close_mapper(DevicePreparationTransaction& transaction) const {
    try {
        cryptsetup_.close(transaction.mapper);
        transaction.cleanup_result = "mapper-closed";
        transaction.mapper.clear();
    } catch (...) {
        transaction.cleanup_result = "mapper-close-failed";
    }
    persist(transaction);
}

} // namespace btrfsbackup::daemon::control
