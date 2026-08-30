// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/TargetStorageRecorder.hpp>

#include <exception>

#include <core/Errors.hpp>

namespace btrfsbackup::backup {

namespace {

ErrorCode warning_code(const std::exception& error) {
    if (const auto* coded = dynamic_cast<const CodedError*>(&error)) {
        return coded->error_code;
    }
    return ErrorCode::BackupFailed;
}

} // namespace

TargetStorageRecorder::TargetStorageRecorder(
    IMountInspector& mounts,
    IFilesystemSpaceProbe& probe,
    ITargetStorageMeasurementStore& store,
    IClock& clock
)
    : mounts_(mounts), probe_(probe), store_(store), clock_(clock) {
}

std::optional<BackupCompletionWarning> TargetStorageRecorder::record(
    const btrfsbackup::config::Profile& profile
) {
    try {
        const std::optional<MountEntry> mount = mount_at(mounts_.inspect(), profile.target.mount_point.value());
        if (!mount.has_value() || mount->fstype != "btrfs" ||
            mount->filesystem_uuid != profile.target.btrfs_uuid.value()) {
            return std::nullopt;
        }
        store_.write(profile, TargetStorageMeasurement{
                                  .space = probe_.measure_verified_mount(profile.target.mount_point.value(), *mount),
                                  .measured_at = clock_.now(),
                              });
        return std::nullopt;
    } catch (const std::exception& error) {
        return BackupCompletionWarning{
            .component = BackupCompletionWarningComponent::TargetStorageMeasurement,
            .error_code = warning_code(error),
            .message = error.what(),
        };
    } catch (...) {
        return BackupCompletionWarning{
            .component = BackupCompletionWarningComponent::TargetStorageMeasurement,
            .error_code = ErrorCode::BackupFailed,
            .message = "unknown target storage measurement error",
        };
    }
}

} // namespace btrfsbackup::backup
