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
    IFilesystemSpaceProbe& probe,
    ITargetStorageMeasurementStore& store,
    IClock& clock
)
    : probe_(probe), store_(store), clock_(clock) {
}

std::optional<BackupCompletionWarning> TargetStorageRecorder::record(
    const btrfsbackup::config::Profile& profile,
    const MountEntry& verified_target_mount
) {
    try {
        store_.write(profile, TargetStorageMeasurement{
                                  .space = probe_.measure_verified_mount(profile.target.mount_point.value(), verified_target_mount),
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
