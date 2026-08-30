// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <backup/model/BackupRunPlan.hpp>
#include <backup/ports/IBackupDiscovery.hpp>
#include <config/model/Profile.hpp>
#include <core/Identifiers.hpp>
#include <core/Cancellation.hpp>

namespace btrfsbackup::backup {

class IBackupPlanBuilder {
  public:
    virtual ~IBackupPlanBuilder() = default;

    [[nodiscard]] virtual BackupRunPlan build(
        const btrfsbackup::config::Profile& profile,
        const BackupPlanningSnapshot& snapshot,
        const RunId& run_id,
        const std::string& snapshot_timestamp,
        CancellationToken& cancellation
    ) const = 0;
};

} // namespace btrfsbackup::backup
