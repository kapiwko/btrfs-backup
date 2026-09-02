// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <daemon/provisioning/ExistingTargetInspection.hpp>
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

class IExistingTargetInspector {
  public:
    virtual ~IExistingTargetInspector() = default;
    [[nodiscard]] virtual provisioning::ExistingTargetInspectionSummary inspect(
        const provisioning::ExistingPartition& partition,
        const std::string& mapper_name,
        const std::filesystem::path& mount_point,
        int credential_fd
    ) = 0;
    virtual void cleanup_session(
        const std::string& mapper_name,
        const std::filesystem::path& mount_point
    ) = 0;
};

class ExistingTargetInspector final : public IExistingTargetInspector {
  public:
    ExistingTargetInspector(
        platform::linux::storage::ICryptsetupOperations& cryptsetup,
        platform::linux::storage::IBlockDeviceMetadataReader& metadata,
        platform::linux::storage::IExistingTargetMountOperations& mounts,
        backup::IBtrfsOperations& btrfs
    );

    [[nodiscard]] provisioning::ExistingTargetInspectionSummary inspect(
        const provisioning::ExistingPartition& partition,
        const std::string& mapper_name,
        const std::filesystem::path& mount_point,
        int credential_fd
    ) override;
    void cleanup_session(
        const std::string& mapper_name,
        const std::filesystem::path& mount_point
    ) override;

  private:
    platform::linux::storage::ICryptsetupOperations& cryptsetup_;
    platform::linux::storage::IBlockDeviceMetadataReader& metadata_;
    platform::linux::storage::IExistingTargetMountOperations& mounts_;
    backup::IBtrfsOperations& btrfs_;
};

} // namespace btrfsbackup::daemon::control
