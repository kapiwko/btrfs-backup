// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include <backup/model/BackupExecution.hpp>
#include <backup/ports/IFilesystemSpaceProbe.hpp>
#include <backup/ports/IMountInspector.hpp>
#include <backup/ports/ITargetStorageMeasurementStore.hpp>
#include <backup/ports/RunContext.hpp>
#include <config/domain/Profile.hpp>

namespace btrfsbackup::backup {

class TargetStorageRecorder {
  public:
    TargetStorageRecorder(
        IMountInspector& mounts,
        IFilesystemSpaceProbe& probe,
        ITargetStorageMeasurementStore& store,
        IClock& clock
    );

    [[nodiscard]] std::optional<BackupCompletionWarning> record(
        const btrfsbackup::config::Profile& profile
    );

  private:
    IMountInspector& mounts_;
    IFilesystemSpaceProbe& probe_;
    ITargetStorageMeasurementStore& store_;
    IClock& clock_;
};

} // namespace btrfsbackup::backup
