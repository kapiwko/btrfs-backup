// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <backup/backup_run_event.hpp>
#include <config/json.hpp>

namespace btrfsbackup {

std::string backup_run_action_kind_name(BackupRunActionKind kind);
std::string backup_run_event_kind_name(BackupRunEventKind kind);

Json build_backup_run_checkpoint_json(const BackupRunCheckpoint& checkpoint);
Json build_backup_run_event_json(const BackupRunEvent& event);

} // namespace btrfsbackup
