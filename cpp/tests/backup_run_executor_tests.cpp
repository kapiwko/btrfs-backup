#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <btrfsbackup/backup_run_executor.hpp>
#include <btrfsbackup/errors.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

std::string action_name(btrfsbackup::BackupRunActionKind kind) {
    return std::to_string(static_cast<int>(kind));
}

class RecordingEffects final : public btrfsbackup::IBackupRunActionEffects {
public:
    std::vector<std::string> calls;

    void execute_action(
        const btrfsbackup::BackupRunAction& action,
        const btrfsbackup::BackupSourceRunPlan& source_plan,
        const btrfsbackup::BackupRunPlan&
    ) override {
        calls.push_back(source_plan.source_id + ":" + action_name(action.kind));
    }
};

class RecordingCheckpoints final : public btrfsbackup::IBackupRunCheckpointStore {
public:
    std::vector<btrfsbackup::BackupRunCheckpoint> checkpoints;

    void write_checkpoint(const btrfsbackup::BackupRunCheckpoint& checkpoint) override {
        checkpoints.push_back(checkpoint);
    }
};

class RecordingEvents final : public btrfsbackup::IBackupRunEventSink {
public:
    std::vector<btrfsbackup::BackupRunEvent> events;
    btrfsbackup::CancellationToken* cancel_after_first_completed = nullptr;
    bool cancelled = false;

    void on_backup_run_event(const btrfsbackup::BackupRunEvent& event) override {
        events.push_back(event);
        if (!cancelled
            && cancel_after_first_completed != nullptr
            && event.kind == btrfsbackup::BackupRunEventKind::ActionCompleted) {
            cancelled = true;
            cancel_after_first_completed->request_cancel();
        }
    }

    bool has_event(btrfsbackup::BackupRunEventKind kind) const {
        return std::any_of(events.begin(), events.end(), [kind](const btrfsbackup::BackupRunEvent& event) {
            return event.kind == kind;
        });
    }
};

class RecordingTransferPipeline final : public btrfsbackup::ITransferPipeline {
public:
    std::vector<btrfsbackup::TransferPipelinePlan> plans;
    btrfsbackup::TransferResult next_result{
        .producer = {
            .started = true,
            .exit_code = 0,
        },
        .consumer = {
            .started = true,
            .exit_code = 0,
        },
    };
    std::uint64_t progress_bytes = 0;

    btrfsbackup::TransferResult run(
        const btrfsbackup::TransferPipelinePlan& plan,
        btrfsbackup::ITransferEventSink& events,
        btrfsbackup::CancellationToken&
    ) override {
        plans.push_back(plan);
        if (progress_bytes > 0) {
            events.on_transfer_event({
                .kind = btrfsbackup::TransferEventKind::Progress,
                .bytes_transferred = progress_bytes,
                .message = "progress",
            });
        }
        return next_result;
    }
};

btrfsbackup::BackupRunAction action(btrfsbackup::BackupRunActionKind kind) {
    return btrfsbackup::BackupRunAction{
        .kind = kind,
        .source_id = "root",
    };
}

btrfsbackup::BackupRunPlan plan_with_actions(std::vector<btrfsbackup::BackupRunAction> actions) {
    btrfsbackup::BackupSourceRunPlan source;
    source.source_id = "root";
    source.local_snapshot_path = "/.snapshots/root/root-2026-08-23T080000Z";
    source.incoming_run_dir = "/mnt/backup/.incoming/root/run-1";
    source.actions = std::move(actions);

    return btrfsbackup::BackupRunPlan{
        .profile_id = "default",
        .run_id = "run-1",
        .sources = {source},
    };
}

void test_full_backup_flow_without_parent() {
    RecordingEffects effects;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::BackupRunExecutor executor(effects, transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::CleanupIncoming),
        action(btrfsbackup::BackupRunActionKind::CreateSnapshot),
        action(btrfsbackup::BackupRunActionKind::SelectParent),
        action(btrfsbackup::BackupRunActionKind::SendReceive),
        action(btrfsbackup::BackupRunActionKind::VerifyReceived),
        action(btrfsbackup::BackupRunActionKind::CommitReceived),
        action(btrfsbackup::BackupRunActionKind::ApplyRemoteRetention),
        action(btrfsbackup::BackupRunActionKind::ApplyLocalRetention),
        action(btrfsbackup::BackupRunActionKind::CleanupSource),
    });

    btrfsbackup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("full flow completed", result.completed, "run should complete");
    test_helpers::expect_eq("full flow actions", std::to_string(result.actions_completed), "9");
    test_helpers::expect_eq("full flow transfer count", std::to_string(transfers.plans.size()), "1");
    const std::vector<std::string>& send_argv = transfers.plans.at(0).producer_argv;
    test_helpers::expect_eq("full send argc", std::to_string(send_argv.size()), "3");
    test_helpers::expect_eq("full send binary", send_argv.at(0), "btrfs");
    test_helpers::expect_eq("full send subcommand", send_argv.at(1), "send");
    test_helpers::expect_eq("full send snapshot", send_argv.at(2), "/.snapshots/root/root-2026-08-23T080000Z");
    test_helpers::expect_eq("full effect count", std::to_string(effects.calls.size()), "7");
    test_helpers::expect_eq("full checkpoint count", std::to_string(checkpoints.checkpoints.size()), "8");
    test_helpers::expect_eq("full last checkpoint", action_name(checkpoints.checkpoints.back().action_kind), action_name(btrfsbackup::BackupRunActionKind::CleanupSource));
}

void test_executes_actions_and_writes_durable_checkpoints() {
    RecordingEffects effects;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::BackupRunExecutor executor(effects, transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::CleanupIncoming),
        action(btrfsbackup::BackupRunActionKind::SelectParent),
        action(btrfsbackup::BackupRunActionKind::CreateSnapshot),
    });

    btrfsbackup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("executor completed", result.completed, "run should complete");
    test_helpers::expect_eq("actions completed", std::to_string(result.actions_completed), "3");
    test_helpers::expect_eq("effect count", std::to_string(effects.calls.size()), "2");
    test_helpers::expect_eq("first effect", effects.calls.at(0), "root:" + action_name(btrfsbackup::BackupRunActionKind::CleanupIncoming));
    test_helpers::expect_eq("second effect", effects.calls.at(1), "root:" + action_name(btrfsbackup::BackupRunActionKind::CreateSnapshot));
    test_helpers::expect_eq("checkpoint count", std::to_string(checkpoints.checkpoints.size()), "2");
    test_helpers::expect_eq("first checkpoint", action_name(checkpoints.checkpoints.at(0).action_kind), action_name(btrfsbackup::BackupRunActionKind::CleanupIncoming));
    test_helpers::expect_eq("second checkpoint", action_name(checkpoints.checkpoints.at(1).action_kind), action_name(btrfsbackup::BackupRunActionKind::CreateSnapshot));
    test_helpers::expect_true("run completed event", events.has_event(btrfsbackup::BackupRunEventKind::RunCompleted), "missing completion event");
}

void test_send_receive_delegates_to_transfer_pipeline() {
    RecordingEffects effects;
    RecordingTransferPipeline transfers;
    transfers.progress_bytes = 8192;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::BackupRunExecutor executor(effects, transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::SendReceive),
    });
    plan.sources.at(0).parent.incremental = true;
    plan.sources.at(0).parent.local_parent = btrfsbackup::SnapshotInfo{
        .path = "/.snapshots/root/root-2026-08-22T080000Z",
    };

    btrfsbackup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("transfer completed", result.completed, "run should complete");
    test_helpers::expect_eq("effect count", std::to_string(effects.calls.size()), "0");
    test_helpers::expect_eq("transfer count", std::to_string(transfers.plans.size()), "1");
    const btrfsbackup::TransferPipelinePlan& transfer_plan = transfers.plans.at(0);
    test_helpers::expect_eq("send binary", transfer_plan.producer_argv.at(0), "btrfs");
    test_helpers::expect_eq("send subcommand", transfer_plan.producer_argv.at(1), "send");
    test_helpers::expect_eq("send parent flag", transfer_plan.producer_argv.at(2), "-p");
    test_helpers::expect_eq("send parent", transfer_plan.producer_argv.at(3), "/.snapshots/root/root-2026-08-22T080000Z");
    test_helpers::expect_eq("receive dir", transfer_plan.consumer_argv.at(2), "/mnt/backup/.incoming/root/run-1");
    test_helpers::expect_eq("transfer checkpoint count", std::to_string(checkpoints.checkpoints.size()), "1");

    auto progress = std::find_if(events.events.begin(), events.events.end(), [](const btrfsbackup::BackupRunEvent& event) {
        return event.kind == btrfsbackup::BackupRunEventKind::TransferProgress;
    });
    test_helpers::expect_true("progress event", progress != events.events.end(), "missing transfer progress event");
    test_helpers::expect_eq("progress bytes", std::to_string(progress->bytes_transferred), "8192");
}

void test_cancels_between_actions() {
    RecordingEffects effects;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    events.cancel_after_first_completed = &cancellation;
    btrfsbackup::BackupRunExecutor executor(effects, transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::CleanupIncoming),
        action(btrfsbackup::BackupRunActionKind::CreateSnapshot),
    });

    btrfsbackup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("run cancelled", result.cancelled, "run should be cancelled");
    test_helpers::expect_eq("actions completed before cancel", std::to_string(result.actions_completed), "1");
    test_helpers::expect_eq("effect count before cancel", std::to_string(effects.calls.size()), "1");
    test_helpers::expect_eq("checkpoint count before cancel", std::to_string(checkpoints.checkpoints.size()), "1");
    test_helpers::expect_true("cancel event", events.has_event(btrfsbackup::BackupRunEventKind::RunCancelled), "missing cancel event");
}

void test_transfer_failure_emits_failed_action() {
    RecordingEffects effects;
    RecordingTransferPipeline transfers;
    transfers.next_result.producer.exit_code = 7;
    transfers.next_result.producer.diagnostics = "send failed";
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::BackupRunExecutor executor(effects, transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::SendReceive),
    });

    test_helpers::expect_validation_error("transfer failure", [&] {
        (void)executor.execute(plan, events, cancellation);
    }, "producer failed with exit code 7");
    test_helpers::expect_eq("failed checkpoint count", std::to_string(checkpoints.checkpoints.size()), "0");
    test_helpers::expect_true("failed action event", events.has_event(btrfsbackup::BackupRunEventKind::ActionFailed), "missing failed action event");
}

} // namespace

int main() {
    test_full_backup_flow_without_parent();
    test_executes_actions_and_writes_durable_checkpoints();
    test_send_receive_delegates_to_transfer_pipeline();
    test_cancels_between_actions();
    test_transfer_failure_emits_failed_action();

    return test_helpers::finish("backup run executor tests");
}
