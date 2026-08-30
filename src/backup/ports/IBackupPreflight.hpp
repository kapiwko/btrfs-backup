// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include <backup/ports/IMountInspector.hpp>
#include <backup/ports/TargetManager.hpp>
#include <config/domain/Profile.hpp>
#include <core/Cancellation.hpp>

namespace btrfsbackup::backup {

struct BackupPreflightResult {
    std::unique_ptr<IMountedTargetSession> target_session;
    MountEntry verified_target_mount;
};

class IBackupPreflight {
  public:
    virtual ~IBackupPreflight() = default;

    [[nodiscard]] virtual BackupPreflightResult run(
        const btrfsbackup::config::Profile& profile,
        TargetMountMode mode,
        CancellationToken& cancellation
    ) = 0;
};

} // namespace btrfsbackup::backup
