// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

#include <provisioning/DevicePreparation.hpp>

namespace btrfsbackup::provisioning {

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
    std::string partition_device_number;
    std::string partition_uuid;
    std::string luks_uuid;
    std::string btrfs_uuid;
    std::string mapper;
    std::string mapper_device_number;
    std::string inspection_mount_point;
    std::string configuration_state = "not-started";
    std::string credentials_state = "not-started";
    std::string profile_reservation_state = "not-held";
    std::string cleanup_result = "not-required";
    bool cancel_requested = false;
    std::vector<std::string> requested_device_access;
    bool requested_mapper_control = false;
    std::uint64_t access_generation = 0;
    std::uint64_t authorized_access_generation = 0;
};

} // namespace btrfsbackup::provisioning
