// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include <daemon/provisioning/StorageTopology.hpp>

namespace btrfsbackup::backup {
class IBtrfsOperations;
}

namespace btrfsbackup::platform::linux::storage {
class IBlockDeviceMetadataReader;
class ICryptsetupOperations;
class IExistingTargetMountOperations;
} // namespace btrfsbackup::platform::linux::storage

namespace btrfsbackup::daemon::control {

struct ExistingTargetInspectionSummary {
    std::string luks_uuid;
    std::string btrfs_uuid;
    std::string partition_uuid;
    std::string repository_id;
    std::uint64_t catalog_generation = 0;
    std::size_t snapshot_count = 0;

    bool operator==(const ExistingTargetInspectionSummary&) const = default;
};

class ExistingTargetInspector {
  public:
    ExistingTargetInspector(
        platform::linux::storage::ICryptsetupOperations& cryptsetup,
        platform::linux::storage::IBlockDeviceMetadataReader& metadata,
        platform::linux::storage::IExistingTargetMountOperations& mounts,
        backup::IBtrfsOperations& btrfs
    );

    [[nodiscard]] ExistingTargetInspectionSummary inspect(
        const provisioning::ExistingPartition& partition,
        const std::string& mapper_name,
        const std::filesystem::path& mount_point,
        int credential_fd
    );

  private:
    platform::linux::storage::ICryptsetupOperations& cryptsetup_;
    platform::linux::storage::IBlockDeviceMetadataReader& metadata_;
    platform::linux::storage::IExistingTargetMountOperations& mounts_;
    backup::IBtrfsOperations& btrfs_;
};

} // namespace btrfsbackup::daemon::control
