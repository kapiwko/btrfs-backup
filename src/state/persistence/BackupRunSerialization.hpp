// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <backup/model/BackupRunCheckpoint.hpp>
#include <backup/model/BackupRunEvent.hpp>
#include <config/json/Json.hpp>

namespace btrfsbackup::state {

// Serialization boundary for persisted backup events and checkpoints.

std::string backup_run_action_kind_name(btrfsbackup::backup::BackupRunActionKind kind);
std::string backup_run_event_kind_name(btrfsbackup::backup::BackupRunEventKind kind);
std::string operation_kind_name(btrfsbackup::backup::OperationKind kind);

btrfsbackup::config::json::Json build_backup_run_checkpoint_json(const btrfsbackup::backup::BackupRunCheckpoint& checkpoint);
btrfsbackup::config::json::Json build_backup_run_event_json(const btrfsbackup::backup::BackupRunEvent& event);

} // namespace btrfsbackup::state
