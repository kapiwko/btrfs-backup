// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/model/BackupPlanningSnapshot.hpp>
#include <config/ApplicationPaths.hpp>
#include <config/model/Profile.hpp>
#include <core/Cancellation.hpp>

namespace btrfsbackup::backup {

class IBackupDiscovery {
  public:
    virtual ~IBackupDiscovery() = default;

    [[nodiscard]] virtual BackupPlanningSnapshot discover(
        const btrfsbackup::config::Profile& profile,
        const btrfsbackup::config::ApplicationPaths& paths,
        CancellationToken& cancellation
    ) const = 0;
};

} // namespace btrfsbackup::backup
