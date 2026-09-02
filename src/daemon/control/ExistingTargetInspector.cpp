// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/ExistingTargetInspector.hpp>

#include <exception>
#include <optional>

#include <backup/ports/IBtrfsOperations.hpp>
#include <core/Errors.hpp>
#include <platform/linux/storage/BlockDeviceMetadata.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>
#include <platform/linux/storage/ExistingTargetMountOperations.hpp>
#include <restore/RepositoryDiscoveryService.hpp>

namespace btrfsbackup::daemon::control {
namespace {

void validate_partition(const provisioning::ExistingPartition& partition) {
    if (!partition.suitable_for_adoption || partition.filesystem.type != "crypto_LUKS" ||
        !partition.partition_uuid || partition.partition_uuid->empty() ||
        !partition.mount_points.empty() || !partition.holders.empty() || partition.active_swap ||
        !partition.blockers.empty())
        throw ValidationError("partition is not suitable for existing target inspection");
}

void finish_session(
    platform::linux::storage::ICryptsetupOperations& cryptsetup,
    platform::linux::storage::IExistingTargetMountOperations& mounts,
    const std::string& mapper_name,
    const std::filesystem::path& mount_point,
    bool mounted,
    bool opened,
    std::exception_ptr pending
) {
    try {
        if (mounted)
            mounts.unmount(mount_point);
    } catch (...) {
        if (!pending)
            pending = std::current_exception();
    }
    try {
        if (opened)
            cryptsetup.close(mapper_name);
    } catch (...) {
        if (!pending)
            pending = std::current_exception();
    }
    if (pending)
        std::rethrow_exception(pending);
}

} // namespace

ExistingTargetInspector::ExistingTargetInspector(
    platform::linux::storage::ICryptsetupOperations& cryptsetup,
    platform::linux::storage::IBlockDeviceMetadataReader& metadata,
    platform::linux::storage::IExistingTargetMountOperations& mounts,
    backup::IBtrfsOperations& btrfs
)
    : cryptsetup_(cryptsetup), metadata_(metadata), mounts_(mounts), btrfs_(btrfs) {
}

provisioning::ExistingTargetInspectionSummary ExistingTargetInspector::inspect(
    const provisioning::ExistingPartition& partition,
    const std::string& mapper_name,
    const std::filesystem::path& mount_point,
    int credential_fd
) {
    validate_partition(partition);
    const std::filesystem::path device = partition.identity.display_path;
    const auto luks = cryptsetup_.inspect_luks2(device);
    if (partition.filesystem.uuid.empty() || partition.filesystem.uuid != luks.uuid)
        throw ValidationError("LUKS2 identity changed before existing target inspection");
    bool opened = false;
    bool mounted = false;
    std::optional<provisioning::ExistingTargetInspectionSummary> summary;
    std::exception_ptr pending;
    try {
        cryptsetup_.open_luks2_read_only(device, mapper_name, credential_fd);
        opened = true;
        const auto mapped = metadata_.read(std::filesystem::path("/dev/mapper") / mapper_name);
        if (mapped.filesystem_type != "btrfs" || mapped.filesystem_uuid.empty())
            throw ValidationError("existing target does not contain a Btrfs filesystem");
        mounts_.mount_btrfs_read_only(std::filesystem::path("/dev/mapper") / mapper_name, mount_point);
        mounted = true;

        restore::RepositoryDiscoveryService discovery([this](const std::filesystem::path& path) {
            const auto metadata = btrfs_.read_snapshot_metadata(path);
            if (!metadata)
                return std::optional<restore::DiscoveredSnapshotMetadata>{};
            return std::optional<restore::DiscoveredSnapshotMetadata>{restore::DiscoveredSnapshotMetadata{
                .is_subvolume = metadata->is_subvolume,
                .readonly = metadata->readonly,
                .uuid = metadata->uuid.value(),
                .received_uuid = metadata->received_uuid.value(),
            }};
        });
        const auto repository = discovery.discover(mount_point);
        if (repository.identity().target_filesystem_uuid != mapped.filesystem_uuid)
            throw ValidationError("repository filesystem identity does not match the existing target");
        summary = provisioning::ExistingTargetInspectionSummary{
            .luks_uuid = luks.uuid,
            .btrfs_uuid = mapped.filesystem_uuid,
            .partition_uuid = partition.partition_uuid.value_or(std::string{}),
            .repository_id = repository.identity().repository_id,
            .catalog_generation = repository.generation(),
            .snapshot_count = repository.snapshots().size(),
        };
    } catch (...) {
        pending = std::current_exception();
    }
    finish_session(cryptsetup_, mounts_, mapper_name, mount_point, mounted, opened, pending);
    return *summary;
}

void ExistingTargetInspector::cleanup_session(
    const std::string& mapper_name,
    const std::filesystem::path& mount_point
) {
    std::exception_ptr pending;
    try {
        mounts_.unmount(mount_point);
    } catch (...) {
        pending = std::current_exception();
    }
    try {
        cryptsetup_.close(mapper_name);
        pending = nullptr;
    } catch (...) {
        if (!pending)
            pending = std::current_exception();
    }
    if (pending)
        std::rethrow_exception(pending);
}

} // namespace btrfsbackup::daemon::control
