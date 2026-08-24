#pragma once

#include <cstdint>
#include <string>

#include <backup/backup_run_action.hpp>

namespace btrfsbackup {

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
    std::string profile_id;
    std::string run_id;
    std::string source_id;
    int source_index = 0;
    BackupRunActionKind action_kind = BackupRunActionKind::CleanupSource;
    std::uint64_t bytes_transferred = 0;
    std::uint64_t bytes_produced = 0;
    std::uint64_t bytes_total_estimated = 0;
    std::uint64_t run_bytes_transferred = 0;
    std::uint64_t delta_bytes = 0;
    std::uint64_t elapsed_ms = 0;
    std::uint64_t speed_bps = 0;
    std::string error_code;
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
    std::string profile_id;
    std::string run_id;
    std::string source_id;
    BackupRunActionKind action_kind = BackupRunActionKind::CleanupSource;
};

class IBackupRunCheckpointStore {
public:
    virtual ~IBackupRunCheckpointStore() = default;
    virtual void write_checkpoint(const BackupRunCheckpoint& checkpoint) = 0;
};

} // namespace btrfsbackup
