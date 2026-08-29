// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/model/backup_run_action.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup::backup {

struct BackupRunCheckpoint {
    ProfileId profile_id;
    RunId run_id;
    SourceId source_id;
    BackupRunActionKind action_kind = BackupRunActionKind::CleanupSource;
};

} // namespace btrfsbackup::backup
