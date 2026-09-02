// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/query/DeviceStateQueryService.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <config/domain/Profile.hpp>
#include <config/json/ProfileDocument.hpp>
#include <core/Identifiers.hpp>
#include <core/RuntimeTime.hpp>
#include <daemon/query/ManagerDocumentReader.hpp>
#include <platform/linux/storage/FilesystemSpaceProbe.hpp>
#include <platform/linux/storage/DeviceInfo.hpp>
#include <platform/linux/storage/MountInfo.hpp>
#include <state/persistence/FileTargetStorageMeasurementStore.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::query {

struct DeviceStateQueryService::Impl {
    explicit Impl(ManagerPaths configured_paths)
        : paths(std::move(configured_paths)),
          owned_mounts(std::make_unique<btrfsbackup::platform::linux::storage::LinuxMountInspector>(paths.mountinfo_path)),
          owned_space_probe(std::make_unique<btrfsbackup::platform::linux::storage::FilesystemSpaceProbe>()),
          owned_storage_reader(std::make_unique<btrfsbackup::state::FileTargetStorageMeasurementStore>(paths.state_root)),
          mounts(*owned_mounts),
          space_probe(*owned_space_probe),
          storage_reader(*owned_storage_reader) {
    }

    Impl(
        ManagerPaths configured_paths,
        btrfsbackup::backup::IMountInspector& configured_mounts,
        btrfsbackup::backup::IFilesystemSpaceProbe& configured_space_probe,
        btrfsbackup::backup::ITargetStorageMeasurementReader& configured_storage_reader
    )
        : paths(std::move(configured_paths)),
          mounts(configured_mounts),
          space_probe(configured_space_probe),
          storage_reader(configured_storage_reader) {
    }

    ManagerPaths paths;
    std::unique_ptr<btrfsbackup::backup::IMountInspector> owned_mounts;
    std::unique_ptr<btrfsbackup::backup::IFilesystemSpaceProbe> owned_space_probe;
    std::unique_ptr<btrfsbackup::backup::ITargetStorageMeasurementReader> owned_storage_reader;
    btrfsbackup::backup::IMountInspector& mounts;
    btrfsbackup::backup::IFilesystemSpaceProbe& space_probe;
    btrfsbackup::backup::ITargetStorageMeasurementReader& storage_reader;
};

DeviceStateQueryService::DeviceStateQueryService(ManagerPaths paths)
    : impl_(std::make_unique<Impl>(std::move(paths))) {
}

DeviceStateQueryService::DeviceStateQueryService(
    ManagerPaths paths,
    btrfsbackup::backup::IMountInspector& mounts,
    btrfsbackup::backup::IFilesystemSpaceProbe& space_probe,
    btrfsbackup::backup::ITargetStorageMeasurementReader& storage_reader
)
    : impl_(std::make_unique<Impl>(std::move(paths), mounts, space_probe, storage_reader)) {
}

DeviceStateQueryService::~DeviceStateQueryService() noexcept = default;

TargetStatus DeviceStateQueryService::get_device_state(
    const std::string& profile_id
) const {
    const ManagerPaths& paths = impl_->paths;
    validate_profile_id(profile_id);
    const fs::path profile_path = paths.config_root / "profiles" / profile_id / "profile.json";
    const btrfsbackup::config::Profile profile = btrfsbackup::config::json::profile_from_json(
        read_manager_json_document(profile_path),
        paths.target_mount_root
    );
    const fs::path mapper = btrfsbackup::platform::linux::storage::mapper_path(
        profile.target.mapper_name.value(),
        paths.mapper_root
    );
    const fs::path mountpoint = paths.target_mount_root / profile_id;
    const std::vector<btrfsbackup::backup::MountEntry> mounts = impl_->mounts.inspect();
    const std::optional<btrfsbackup::backup::MountEntry> mounted_entry = btrfsbackup::backup::mount_at(mounts, mountpoint);
    const bool mount_present = mounted_entry.has_value();
    const bool target_mounted = mount_present && mounted_entry->fstype == "btrfs" &&
        mounted_entry->filesystem_uuid == profile.target.btrfs_uuid.value() &&
        btrfsbackup::backup::mount_uses_mapper(mounts, mountpoint, mapper);
    const bool connected = fs::exists(profile.target.device);
    const bool unlocked = connected && fs::exists(mapper);

    std::string state = "disconnected";
    bool safe_to_remove = false;
    if (target_mounted) {
        state = "mounted";
    } else if (mount_present) {
        state = "unexpected-mount";
    } else if (unlocked) {
        state = "unlocked";
    } else if (connected) {
        state = "connected";
        safe_to_remove = true;
    }
    std::optional<btrfsbackup::backup::TargetStorageMeasurement> measurement;
    bool live = false;
    if (target_mounted) {
        try {
            measurement = btrfsbackup::backup::TargetStorageMeasurement{
                .space = impl_->space_probe.measure_verified_mount(mountpoint, *mounted_entry),
                .measured_at = std::chrono::system_clock::now(),
            };
            live = true;
        } catch (const std::exception&) {
        }
    }
    if (!measurement.has_value()) {
        measurement = impl_->storage_reader.read_matching(profile);
    }

    std::optional<btrfsbackup::state::document::TargetStorageStatusV1> storage;
    if (measurement.has_value()) {
        const bool below_minimum = profile.settings.minimum_target_free_bytes.value() > 0 &&
            measurement->space.available_bytes < profile.settings.minimum_target_free_bytes.value();
        storage = btrfsbackup::state::document::TargetStorageStatusV1{
            .capacity_bytes = measurement->space.capacity_bytes,
            .used_bytes = measurement->space.used_bytes(),
            .available_bytes = measurement->space.available_bytes,
            .usage_percent = measurement->space.usage_percent(),
            .measured_at = measurement->measured_at,
            .live = live,
            .space_state = below_minimum
                ? btrfsbackup::state::document::TargetSpaceState::BelowConfiguredMinimum
                : btrfsbackup::state::document::TargetSpaceState::Normal,
        };
    }

    return {
        .profile_id = profile_id,
        .target_name = std::string(profile.target.mapper_name.value()),
        .state = state,
        .connected = connected,
        .unlocked = unlocked,
        .mounted = target_mounted,
        .safe_to_remove = safe_to_remove,
        .storage = storage,
    };
}

} // namespace btrfsbackup::daemon::query
