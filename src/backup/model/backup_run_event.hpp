// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <backup/model/backup_run_action.hpp>
#include <core/identifiers.hpp>
#include <core/error_code.hpp>

namespace btrfsbackup::backup {

enum class BackupRunEventKind {
    RunStarted,
    SourceStarted,
    ActionStarted,
    TransferProgress,
    ActionCompleted,
    ActionFailed,
    CheckpointWritten,
    SourceCompleted,
    RunCompleted,
    RunCancelled,
};

struct BackupRunEvent {
    BackupRunEventKind kind = BackupRunEventKind::RunStarted;
    ProfileId profile_id;
    RunId run_id;
    std::optional<SourceId> source_id;
    int source_index = 0;
    BackupRunActionKind action_kind = BackupRunActionKind::CleanupSource;
    std::uint64_t bytes_transferred = 0;
    std::uint64_t bytes_produced = 0;
    std::uint64_t bytes_total_estimated = 0;
    std::uint64_t run_bytes_transferred = 0;
    std::uint64_t delta_bytes = 0;
    std::uint64_t elapsed_ms = 0;
    std::uint64_t speed_bps = 0;
    std::optional<ErrorCode> error_code;
    std::string message;
};

class IBackupRunEventSink {
  public:
    virtual ~IBackupRunEventSink() = default;
    virtual void on_backup_run_event(const BackupRunEvent& event) = 0;
};

class NullBackupRunEventSink final : public IBackupRunEventSink {
  public:
    void on_backup_run_event(const BackupRunEvent& event) override;
};

struct BackupRunCheckpoint {
    ProfileId profile_id;
    RunId run_id;
    SourceId source_id;
    BackupRunActionKind action_kind = BackupRunActionKind::CleanupSource;
};

class IBackupRunCheckpointStore {
  public:
    virtual ~IBackupRunCheckpointStore() = default;
    virtual void write_checkpoint(const BackupRunCheckpoint& checkpoint) = 0;
};

} // namespace btrfsbackup::backup
