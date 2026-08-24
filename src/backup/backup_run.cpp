#include <backup/backup_run.hpp>

#include <stdexcept>
#include <utility>

namespace btrfsbackup {

BackupRun::BackupRun(
    BackupRunPlan plan,
    IBackupRunActionEffects& action_effects,
    IAsyncTransferPipeline& transfer_pipeline,
    IBackupRunCheckpointStore& checkpoints
)
    : plan_(std::move(plan)),
      executor_(action_effects, transfer_pipeline, checkpoints) {
}

const BackupRunPlan& BackupRun::plan() const noexcept {
    return plan_;
}

bool BackupRun::started() const noexcept {
    return started_;
}

BackupRunExecutionResult BackupRun::execute(
    IBackupRunEventSink& events,
    CancellationToken& cancellation
) {
    if (started_) {
        throw std::logic_error("backup run cannot be executed more than once");
    }
    started_ = true;
    return executor_.execute(plan_, events, cancellation);
}

} // namespace btrfsbackup
