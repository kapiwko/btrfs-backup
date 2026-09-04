// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/ExistingTargetInspector.hpp>

#include <exception>
#include <filesystem>
#include <optional>

#include <backup/ports/IBtrfsOperations.hpp>
#include <core/Errors.hpp>
#include <platform/linux/storage/BlockDeviceMetadata.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>
#include <platform/linux/storage/provisioning/ExistingTargetMountOperations.hpp>
#include <restore/RepositoryDiscoveryService.hpp>
#include <restore/RestoreError.hpp>

namespace btrfsbackup::daemon::control {
namespace {

bool is_directory_without_symlink(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status);
}

provisioning::ExistingTargetClassification classify_repositoryless_root(
    const std::filesystem::path& root,
    std::string& diagnostic_code
) {
    std::error_code error;
    std::filesystem::directory_iterator entries(root, std::filesystem::directory_options::skip_permission_denied, error);
    if (error) {
        diagnostic_code = "repository-root-unreadable";
        return provisioning::ExistingTargetClassification::ForeignOrInvalidRepository;
    }
    bool empty = true;
    bool legacy = is_directory_without_symlink(root / "snapshots");
    for (const auto& entry : entries) {
        empty = false;
        if (is_directory_without_symlink(entry.path()) && is_directory_without_symlink(entry.path() / "snapshots"))
            legacy = true;
    }
    if (empty) {
        diagnostic_code = "repository-not-found";
        return provisioning::ExistingTargetClassification::EmptyFilesystem;
    }
    if (legacy) {
        diagnostic_code = "legacy-repository-layout";
        return provisioning::ExistingTargetClassification::LegacyRepository;
    }
    diagnostic_code = "repository-not-found";
    return provisioning::ExistingTargetClassification::ForeignOrInvalidRepository;
}

void validate_partition(const provisioning::ExistingPartition& partition) {
    if (!partition.suitable_for_adoption || partition.filesystem.type != "crypto_LUKS" ||
        !partition.partition_uuid || partition.partition_uuid->empty() ||
        !partition.mount_points.empty() || !partition.holders.empty() || partition.active_swap ||
        !partition.blockers.empty())
        throw ValidationError("partition is not suitable for existing target inspection");
}

void finish_session(
    platform::linux::storage::ICryptsetupOperations& cryptsetup,
    platform::linux::storage::provisioning::IExistingTargetMountOperations& mounts,
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
    platform::linux::storage::provisioning::IExistingTargetMountOperations& mounts,
    backup::IBtrfsOperations& btrfs
)
    : cryptsetup_(cryptsetup), metadata_(metadata), mounts_(mounts), btrfs_(btrfs) {
}

provisioning::ExistingTargetInspectionSummary ExistingTargetInspector::inspect(
    const provisioning::ExistingPartition& partition,
    const std::string& mapper_name,
    const std::filesystem::path& mount_point,
    int credential_fd,
    const MapperReady& mapper_ready
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
        const auto mapper_path = std::filesystem::path("/dev/mapper") / mapper_name;
        if (mapper_ready)
            mapper_ready(mapper_path);
        const auto mapped = metadata_.read(mapper_path);
        if (mapped.filesystem_type != "btrfs" || mapped.filesystem_uuid.empty()) {
            summary = provisioning::ExistingTargetInspectionSummary{
                .classification = provisioning::ExistingTargetClassification::NotBtrfsFilesystem,
                .diagnostic_code = "not-btrfs-filesystem",
                .luks_uuid = luks.uuid,
                .btrfs_uuid = {},
                .partition_uuid = partition.partition_uuid.value_or(std::string{}),
                .repository_id = {},
                .catalog_generation = 0,
                .snapshot_count = 0,
            };
        } else {
            mounts_.mount_btrfs_read_only(mapper_path, mount_point);
            mounted = true;

            std::error_code repository_error;
            const bool repository_exists = std::filesystem::exists(mount_point / "repository.json", repository_error);
            if (repository_error || !repository_exists) {
                std::string diagnostic_code;
                const auto classification = repository_error
                    ? provisioning::ExistingTargetClassification::ForeignOrInvalidRepository
                    : classify_repositoryless_root(mount_point, diagnostic_code);
                if (repository_error)
                    diagnostic_code = "repository-metadata-inaccessible";
                summary = provisioning::ExistingTargetInspectionSummary{
                    .classification = classification,
                    .diagnostic_code = std::move(diagnostic_code),
                    .luks_uuid = luks.uuid,
                    .btrfs_uuid = mapped.filesystem_uuid,
                    .partition_uuid = partition.partition_uuid.value_or(std::string{}),
                    .repository_id = {},
                    .catalog_generation = 0,
                    .snapshot_count = 0,
                };
            } else {
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
                try {
                    const auto repository = discovery.discover(mount_point);
                    if (repository.identity().target_filesystem_uuid != mapped.filesystem_uuid) {
                        summary = provisioning::ExistingTargetInspectionSummary{
                            .classification = provisioning::ExistingTargetClassification::ForeignOrInvalidRepository,
                            .diagnostic_code = "repository-filesystem-identity-mismatch",
                            .luks_uuid = luks.uuid,
                            .btrfs_uuid = mapped.filesystem_uuid,
                            .partition_uuid = partition.partition_uuid.value_or(std::string{}),
                            .repository_id = {},
                            .catalog_generation = 0,
                            .snapshot_count = 0,
                        };
                    } else {
                        summary = provisioning::ExistingTargetInspectionSummary{
                            .classification = provisioning::ExistingTargetClassification::CompatibleRepository,
                            .diagnostic_code = {},
                            .luks_uuid = luks.uuid,
                            .btrfs_uuid = mapped.filesystem_uuid,
                            .partition_uuid = partition.partition_uuid.value_or(std::string{}),
                            .repository_id = repository.identity().repository_id,
                            .catalog_generation = repository.generation(),
                            .snapshot_count = repository.snapshots().size(),
                        };
                    }
                } catch (const restore::RestoreError& error) {
                    summary = provisioning::ExistingTargetInspectionSummary{
                        .classification = error.code() == restore::RestoreErrorCode::RepositoryFormatUnsupported
                            ? provisioning::ExistingTargetClassification::UnsupportedRepository
                            : provisioning::ExistingTargetClassification::ForeignOrInvalidRepository,
                        .diagnostic_code = restore::restore_error_code_name(error.code()),
                        .luks_uuid = luks.uuid,
                        .btrfs_uuid = mapped.filesystem_uuid,
                        .partition_uuid = partition.partition_uuid.value_or(std::string{}),
                        .repository_id = {},
                        .catalog_generation = 0,
                        .snapshot_count = 0,
                    };
                }
            }
        }
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
    } catch (...) {
        if (!pending)
            pending = std::current_exception();
    }
    if (pending)
        std::rethrow_exception(pending);
}

} // namespace btrfsbackup::daemon::control
