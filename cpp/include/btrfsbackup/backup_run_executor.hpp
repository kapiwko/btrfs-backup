#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <btrfsbackup/backup_run_plan.hpp>
#include <btrfsbackup/transfer_pipeline.hpp>

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
    BackupRunActionKind action_kind = BackupRunActionKind::CleanupSource;
    std::uint64_t bytes_transferred = 0;
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

class IBackupRunActionEffects {
public:
    virtual ~IBackupRunActionEffects() = default;
    virtual void execute_action(
        const BackupRunAction& action,
        const BackupSourceRunPlan& source_plan,
        const BackupRunPlan& run_plan
    ) = 0;
};

struct BackupRunExecutionResult {
    bool completed = false;
    bool cancelled = false;
    std::size_t actions_completed = 0;
};

class BackupRunExecutor {
public:
    BackupRunExecutor(
        IBackupRunActionEffects& action_effects,
        ITransferPipeline& transfer_pipeline,
        IBackupRunCheckpointStore& checkpoints
    );

    BackupRunExecutionResult execute(
        const BackupRunPlan& plan,
        IBackupRunEventSink& events,
        CancellationToken& cancellation
    );

private:
    IBackupRunActionEffects& action_effects_;
    ITransferPipeline& transfer_pipeline_;
    IBackupRunCheckpointStore& checkpoints_;
};

bool backup_run_action_writes_checkpoint(BackupRunActionKind kind);

} // namespace btrfsbackup
