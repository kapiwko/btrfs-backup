// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <backup/backup_run_executor.hpp>
#include <config/errors.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

std::string action_name(btrfsbackup::BackupRunActionKind kind) {
    return std::to_string(static_cast<int>(kind));
}

class RecordingActionHandler final : public btrfsbackup::IBackupRunActionHandler {
  public:
    std::vector<std::string> calls;
    bool should_throw = false;
    bool recovery_required = false;
    bool operation_cancelled = false;
    std::string coded_error_code;
    btrfsbackup::BackupRunActionKind throw_on = btrfsbackup::BackupRunActionKind::CleanupSource;

    void handle(
        const btrfsbackup::BackupRunAction& action,
        const btrfsbackup::BackupRunPlan&,
        btrfsbackup::CancellationToken&
    ) override {
        const btrfsbackup::BackupRunActionKind kind = btrfsbackup::backup_run_action_kind(action);
        calls.push_back(
            std::string(btrfsbackup::backup_run_action_source_id(action).value()) + ":" + action_name(kind)
        );
        if (should_throw && kind == throw_on) {
            if (operation_cancelled) {
                throw btrfsbackup::OperationCancelledError("hook cancelled");
            }
            if (recovery_required) {
                throw btrfsbackup::RecoveryRequiredError(
                    "repository.recovery_required",
                    "commit verification failed; cleanup failed; repository requires recovery"
                );
            }
            if (!coded_error_code.empty()) {
                throw btrfsbackup::CodedValidationError(coded_error_code, "coded action failure");
            }
            throw btrfsbackup::ValidationError("injected action failure: " + action_name(kind));
        }
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
    std::vector<std::uint64_t> progress_bytes_by_run;
    bool cancel_during_run = false;

    btrfsbackup::TransferResult run(
        const btrfsbackup::TransferPipelinePlan& plan,
        btrfsbackup::ITransferEventSink& events,
        btrfsbackup::CancellationToken&
    ) override {
        plans.push_back(plan);
        std::uint64_t reported_progress_bytes = progress_bytes;
        if (!progress_bytes_by_run.empty() && plans.size() <= progress_bytes_by_run.size()) {
            reported_progress_bytes = progress_bytes_by_run.at(plans.size() - 1);
        }
        if (reported_progress_bytes > 0) {
            events.on_transfer_event({
                .kind = btrfsbackup::TransferEventKind::Progress,
                .bytes_transferred = reported_progress_bytes,
                .bytes_produced = reported_progress_bytes,
                .delta_bytes = reported_progress_bytes,
                .elapsed_ms = 1000,
                .speed_bps = reported_progress_bytes,
                .message = "progress",
            });
        }
        if (cancel_during_run) {
            next_result.cancelled = true;
        }
        btrfsbackup::TransferResult result = next_result;
        if (result.bytes_transferred == 0) {
            result.bytes_transferred = reported_progress_bytes;
        }
        if (result.bytes_produced == 0) {
            result.bytes_produced = reported_progress_bytes;
        }
        result.bytes_total_estimated = plan.bytes_total_estimated;
        return result;
    }
};

btrfsbackup::BackupRunAction action(btrfsbackup::BackupRunActionKind kind) {
    using namespace btrfsbackup;
    const SourceId source_id{"root"};
    const fs::path local_snapshot = "/.snapshots/root/root-2026-08-23T080000Z";
    const fs::path incoming = "/mnt/backup/.incoming/root/run-1";
    switch (kind) {
    case BackupRunActionKind::RecoverPending:
        return RecoverPendingAction{source_id, PendingRecoveryPlan{}};
    case BackupRunActionKind::CleanupIncoming:
        return CleanupIncomingAction{source_id, incoming.parent_path()};
    case BackupRunActionKind::BeforeSnapshotHook:
        return RunHookAction{source_id, HookPhase::BeforeSnapshot, ProfileHookCommand{"hook", {}, 30}};
    case BackupRunActionKind::CreateSnapshot:
        return CreateSnapshotAction{
            source_id,
            "/source",
            local_snapshot.parent_path(),
            local_snapshot,
            "/mnt/backup/root/snapshot",
            "/state",
            RunId{"run-1"},
        };
    case BackupRunActionKind::AfterSnapshotHook:
        return RunHookAction{source_id, HookPhase::AfterSnapshot, ProfileHookCommand{"hook", {}, 30}};
    case BackupRunActionKind::SelectParent:
        return SelectParentAction{source_id, std::nullopt};
    case BackupRunActionKind::SendReceive:
        return SendReceiveAction{source_id, local_snapshot, std::nullopt, "/mnt/backup/root", incoming};
    case BackupRunActionKind::VerifyReceived:
        return VerifyReceivedAction{source_id, local_snapshot, incoming / local_snapshot.filename()};
    case BackupRunActionKind::CommitReceived:
        return CommitReceivedAction{
            source_id,
            local_snapshot,
            incoming / local_snapshot.filename(),
            "/mnt/backup/root/snapshot",
        };
    case BackupRunActionKind::ApplyRemoteRetention:
        return ApplyRemoteRetentionAction{source_id, RetentionPlan{}};
    case BackupRunActionKind::ApplyLocalRetention:
        return ApplyLocalRetentionAction{source_id, RetentionPlan{}};
    case BackupRunActionKind::CleanupSource:
        return CleanupSourceAction{source_id, incoming / local_snapshot.filename(), incoming, "/state/pending", "/state"};
    }
    throw std::logic_error("unsupported action kind");
}

btrfsbackup::BackupRunPlan plan_with_actions(std::vector<btrfsbackup::BackupRunAction> actions) {
    btrfsbackup::BackupSourceRunPlan source{.source_id = btrfsbackup::SourceId{"root"}};
    source.local_snapshot_path = "/.snapshots/root/root-2026-08-23T080000Z";
    source.incoming_run_dir = "/mnt/backup/.incoming/root/run-1";
    source.actions = std::move(actions);

    return btrfsbackup::BackupRunPlan{
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"run-1"},
        .sources = {source},
    };
}

void test_full_backup_flow_without_parent() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

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
    test_helpers::expect_eq("full send argc", std::to_string(send_argv.size()), "6");
    test_helpers::expect_eq("full send binary", send_argv.at(0), "btrfs");
    test_helpers::expect_eq("full send subcommand", send_argv.at(1), "send");
    test_helpers::expect_eq("full send protocol flag", send_argv.at(2), "--proto");
    test_helpers::expect_eq("full send protocol", send_argv.at(3), "2");
    test_helpers::expect_eq("full send compressed data", send_argv.at(4), "--compressed-data");
    test_helpers::expect_eq("full send snapshot", send_argv.at(5), "/.snapshots/root/root-2026-08-23T080000Z");
    test_helpers::expect_eq("full effect count", std::to_string(handler.calls.size()), "8");
    test_helpers::expect_eq("full checkpoint count", std::to_string(checkpoints.checkpoints.size()), "8");
    test_helpers::expect_eq("full last checkpoint", action_name(checkpoints.checkpoints.back().action_kind), action_name(btrfsbackup::BackupRunActionKind::CleanupSource));
}

void test_executes_actions_and_writes_durable_checkpoints() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::CleanupIncoming),
        action(btrfsbackup::BackupRunActionKind::SelectParent),
        action(btrfsbackup::BackupRunActionKind::CreateSnapshot),
    });

    btrfsbackup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("executor completed", result.completed, "run should complete");
    test_helpers::expect_eq("actions completed", std::to_string(result.actions_completed), "3");
    test_helpers::expect_eq("effect count", std::to_string(handler.calls.size()), "2");
    test_helpers::expect_eq("first effect", handler.calls.at(0), "root:" + action_name(btrfsbackup::BackupRunActionKind::CleanupIncoming));
    test_helpers::expect_eq("second effect", handler.calls.at(1), "root:" + action_name(btrfsbackup::BackupRunActionKind::CreateSnapshot));
    test_helpers::expect_eq("checkpoint count", std::to_string(checkpoints.checkpoints.size()), "2");
    test_helpers::expect_eq("first checkpoint", action_name(checkpoints.checkpoints.at(0).action_kind), action_name(btrfsbackup::BackupRunActionKind::CleanupIncoming));
    test_helpers::expect_eq("second checkpoint", action_name(checkpoints.checkpoints.at(1).action_kind), action_name(btrfsbackup::BackupRunActionKind::CreateSnapshot));
    test_helpers::expect_true("run completed event", events.has_event(btrfsbackup::BackupRunEventKind::RunCompleted), "missing completion event");
}

void test_pending_recovery_runs_before_source_cleanup() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::RecoverPending),
        action(btrfsbackup::BackupRunActionKind::CleanupIncoming),
        action(btrfsbackup::BackupRunActionKind::CreateSnapshot),
    });

    btrfsbackup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("pending recovery completed", result.completed, "run should complete");
    test_helpers::expect_eq("pending recovery first effect", handler.calls.at(0), "root:" + action_name(btrfsbackup::BackupRunActionKind::RecoverPending));
    test_helpers::expect_eq("pending recovery second effect", handler.calls.at(1), "root:" + action_name(btrfsbackup::BackupRunActionKind::CleanupIncoming));
    test_helpers::expect_eq("pending recovery checkpoint count", std::to_string(checkpoints.checkpoints.size()), "3");
    test_helpers::expect_eq("pending recovery first checkpoint", action_name(checkpoints.checkpoints.at(0).action_kind), action_name(btrfsbackup::BackupRunActionKind::RecoverPending));
}

void test_send_receive_delegates_to_transfer_pipeline() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    transfers.progress_bytes = 8192;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        btrfsbackup::SendReceiveAction{
            btrfsbackup::SourceId{"root"},
            "/.snapshots/root/root-2026-08-23T080000Z",
            fs::path{"/.snapshots/root/root-2026-08-22T080000Z"},
            "/mnt/backup/root",
            "/mnt/backup/.incoming/root/run-1",
        },
    });

    btrfsbackup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("transfer completed", result.completed, "run should complete");
    test_helpers::expect_eq("effect count", std::to_string(handler.calls.size()), "1");
    test_helpers::expect_eq("prepare receive effect", handler.calls.at(0), "root:" + action_name(btrfsbackup::BackupRunActionKind::SendReceive));
    test_helpers::expect_eq("transfer count", std::to_string(transfers.plans.size()), "1");
    const btrfsbackup::TransferPipelinePlan& transfer_plan = transfers.plans.at(0);
    test_helpers::expect_eq("send binary", transfer_plan.producer_argv.at(0), "btrfs");
    test_helpers::expect_eq("send subcommand", transfer_plan.producer_argv.at(1), "send");
    test_helpers::expect_eq("send protocol flag", transfer_plan.producer_argv.at(2), "--proto");
    test_helpers::expect_eq("send protocol", transfer_plan.producer_argv.at(3), "2");
    test_helpers::expect_eq("send compressed data", transfer_plan.producer_argv.at(4), "--compressed-data");
    test_helpers::expect_eq("send parent flag", transfer_plan.producer_argv.at(5), "-p");
    test_helpers::expect_eq("send parent", transfer_plan.producer_argv.at(6), "/.snapshots/root/root-2026-08-22T080000Z");
    test_helpers::expect_eq("receive dir", transfer_plan.consumer_argv.at(2), "/mnt/backup/.incoming/root/run-1");
    test_helpers::expect_eq("transfer checkpoint count", std::to_string(checkpoints.checkpoints.size()), "1");

    auto progress = std::find_if(events.events.begin(), events.events.end(), [](const btrfsbackup::BackupRunEvent& event) {
        return event.kind == btrfsbackup::BackupRunEventKind::TransferProgress;
    });
    test_helpers::expect_true("progress event", progress != events.events.end(), "missing transfer progress event");
    test_helpers::expect_eq("progress bytes", std::to_string(progress->bytes_transferred), "8192");
    test_helpers::expect_eq("progress delta", std::to_string(progress->delta_bytes), "8192");
}

void test_transfer_plan_estimates_snapshot_bytes() {
    fs::path root = test_helpers::test_root("backup-run-executor", "estimate-bytes");
    fs::create_directories(root / ".snapshots" / "root" / "dir");
    test_helpers::write_file(root / ".snapshots" / "root" / "file-a", "12345");
    test_helpers::write_file(root / ".snapshots" / "root" / "dir" / "file-b", "1234567");

    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        btrfsbackup::SendReceiveAction{
            btrfsbackup::SourceId{"root"},
            root / ".snapshots" / "root",
            std::nullopt,
            "/mnt/backup/root",
            "/mnt/backup/.incoming/root/run-1",
        },
    });

    btrfsbackup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("estimate run completed", result.completed, "run should complete");
    test_helpers::expect_eq("estimated bytes", std::to_string(transfers.plans.at(0).bytes_total_estimated), "12");
    fs::remove_all(root);
}

void test_multi_source_progress_accumulates_run_bytes() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    transfers.progress_bytes_by_run = {1000, 2500};
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupSourceRunPlan home{.source_id = btrfsbackup::SourceId{"home"}};
    home.local_snapshot_path = "/.snapshots/home/home-2026-08-23T080000Z";
    home.incoming_run_dir = "/mnt/backup/.incoming/home/run-1";
    home.actions = {
        btrfsbackup::SendReceiveAction{
            home.source_id,
            home.local_snapshot_path,
            std::nullopt,
            "/mnt/backup/home",
            home.incoming_run_dir,
        },
    };

    btrfsbackup::BackupSourceRunPlan root{.source_id = btrfsbackup::SourceId{"root"}};
    root.local_snapshot_path = "/.snapshots/root/root-2026-08-23T080000Z";
    root.incoming_run_dir = "/mnt/backup/.incoming/root/run-1";
    root.actions = {
        btrfsbackup::SendReceiveAction{
            root.source_id,
            root.local_snapshot_path,
            std::nullopt,
            "/mnt/backup/root",
            root.incoming_run_dir,
        },
    };

    btrfsbackup::BackupRunPlan plan{
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"run-1"},
        .sources = {home, root},
    };

    btrfsbackup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("multi progress completed", result.completed, "run should complete");
    std::vector<btrfsbackup::BackupRunEvent> progress_events;
    std::copy_if(events.events.begin(), events.events.end(), std::back_inserter(progress_events), [](const btrfsbackup::BackupRunEvent& event) {
        return event.kind == btrfsbackup::BackupRunEventKind::TransferProgress;
    });
    test_helpers::expect_eq("multi progress count", std::to_string(progress_events.size()), "2");
    test_helpers::expect_eq("first source index", std::to_string(progress_events.at(0).source_index), "1");
    test_helpers::expect_eq("first run bytes", std::to_string(progress_events.at(0).run_bytes_transferred), "1000");
    test_helpers::expect_eq("second source index", std::to_string(progress_events.at(1).source_index), "2");
    test_helpers::expect_eq("second run bytes", std::to_string(progress_events.at(1).run_bytes_transferred), "3500");
}

void test_cancels_between_actions() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    events.cancel_after_first_completed = &cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::CleanupIncoming),
        action(btrfsbackup::BackupRunActionKind::CreateSnapshot),
    });

    btrfsbackup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("run cancelled", result.cancelled, "run should be cancelled");
    test_helpers::expect_eq("actions completed before cancel", std::to_string(result.actions_completed), "1");
    test_helpers::expect_eq("effect count before cancel", std::to_string(handler.calls.size()), "1");
    test_helpers::expect_eq("checkpoint count before cancel", std::to_string(checkpoints.checkpoints.size()), "1");
    test_helpers::expect_true("cancel event", events.has_event(btrfsbackup::BackupRunEventKind::RunCancelled), "missing cancel event");
}

void test_cancels_during_transfer_without_checkpointing_transfer() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    transfers.cancel_during_run = true;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::CreateSnapshot),
        action(btrfsbackup::BackupRunActionKind::SendReceive),
        action(btrfsbackup::BackupRunActionKind::VerifyReceived),
    });

    btrfsbackup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("transfer cancellation result", result.cancelled, "run should be cancelled");
    test_helpers::expect_eq("transfer cancellation completed actions", std::to_string(result.actions_completed), "1");
    test_helpers::expect_eq("transfer cancellation checkpoint count", std::to_string(checkpoints.checkpoints.size()), "1");
    test_helpers::expect_eq("transfer cancellation last checkpoint", action_name(checkpoints.checkpoints.back().action_kind), action_name(btrfsbackup::BackupRunActionKind::CreateSnapshot));
    test_helpers::expect_true("transfer cancellation event", events.has_event(btrfsbackup::BackupRunEventKind::RunCancelled), "missing cancel event");
}

void test_transfer_failure_emits_failed_action() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    transfers.next_result.producer.exit_code = 7;
    transfers.next_result.producer.diagnostics = "send failed";
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::SendReceive),
    });

    test_helpers::expect_validation_error("transfer failure", [&] {
        (void)executor.execute(plan, events, cancellation);
    }, "producer failed with exit code 7");
    test_helpers::expect_eq("failed checkpoint count", std::to_string(checkpoints.checkpoints.size()), "0");
    test_helpers::expect_true("failed action event", events.has_event(btrfsbackup::BackupRunEventKind::ActionFailed), "missing failed action event");
    auto failed = std::find_if(events.events.begin(), events.events.end(), [](const btrfsbackup::BackupRunEvent& event) {
        return event.kind == btrfsbackup::BackupRunEventKind::ActionFailed;
    });
    test_helpers::expect_true("transfer failed action event", failed != events.events.end(), "missing failed action event");
    test_helpers::expect_eq("transfer failed error code", btrfsbackup::error_code_name(*failed->error_code), "transfer.producer_failed");
}

void test_receive_failure_is_reported_separately() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    transfers.next_result.consumer.exit_code = 9;
    transfers.next_result.consumer.diagnostics = "receive failed";
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::SendReceive),
    });

    test_helpers::expect_validation_error("receive failure", [&] {
        (void)executor.execute(plan, events, cancellation);
    }, "consumer failed with exit code 9");
    test_helpers::expect_eq("receive failure checkpoint count", std::to_string(checkpoints.checkpoints.size()), "0");
    auto failed = std::find_if(events.events.begin(), events.events.end(), [](const btrfsbackup::BackupRunEvent& event) {
        return event.kind == btrfsbackup::BackupRunEventKind::ActionFailed;
    });
    test_helpers::expect_true("receive failed action event", failed != events.events.end(), "missing failed action event");
    test_helpers::expect_eq("receive failed error code", btrfsbackup::error_code_name(*failed->error_code), "transfer.consumer_failed");
    test_helpers::expect_contains("receive failed message", failed->message, "consumer failed with exit code 9");
}

void test_commit_failure_after_successful_transfer_keeps_verify_checkpoint() {
    RecordingActionHandler handler;
    handler.should_throw = true;
    handler.throw_on = btrfsbackup::BackupRunActionKind::CommitReceived;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::CreateSnapshot),
        action(btrfsbackup::BackupRunActionKind::SendReceive),
        action(btrfsbackup::BackupRunActionKind::VerifyReceived),
        action(btrfsbackup::BackupRunActionKind::CommitReceived),
    });

    test_helpers::expect_validation_error("commit failure", [&] {
        (void)executor.execute(plan, events, cancellation);
    }, "injected action failure");
    test_helpers::expect_eq("commit failure transfer count", std::to_string(transfers.plans.size()), "1");
    test_helpers::expect_eq("commit failure checkpoint count", std::to_string(checkpoints.checkpoints.size()), "3");
    test_helpers::expect_eq("commit failure last checkpoint", action_name(checkpoints.checkpoints.back().action_kind), action_name(btrfsbackup::BackupRunActionKind::VerifyReceived));
    test_helpers::expect_true("commit failed action event", events.has_event(btrfsbackup::BackupRunEventKind::ActionFailed), "missing failed action event");
}

void test_commit_cleanup_failure_emits_recovery_required_code() {
    RecordingActionHandler handler;
    handler.should_throw = true;
    handler.recovery_required = true;
    handler.throw_on = btrfsbackup::BackupRunActionKind::CommitReceived;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::CommitReceived),
    });

    test_helpers::expect_validation_error("commit cleanup failure", [&] {
        (void)executor.execute(plan, events, cancellation);
    }, "repository requires recovery");
    auto failed = std::find_if(events.events.begin(), events.events.end(), [](const btrfsbackup::BackupRunEvent& event) {
        return event.kind == btrfsbackup::BackupRunEventKind::ActionFailed;
    });
    test_helpers::expect_true("commit cleanup failed event", failed != events.events.end(), "missing failed action event");
    test_helpers::expect_eq("commit cleanup error code", btrfsbackup::error_code_name(*failed->error_code), "repository.recovery_required");
}

void test_hook_timeout_emits_stable_error_code() {
    RecordingActionHandler handler;
    handler.should_throw = true;
    handler.throw_on = btrfsbackup::BackupRunActionKind::BeforeSnapshotHook;
    handler.coded_error_code = "hook.before_snapshot_timeout";
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::BeforeSnapshotHook),
    });

    test_helpers::expect_validation_error("hook timeout", [&] {
        (void)executor.execute(plan, events, cancellation);
    }, "coded action failure");
    auto failed = std::find_if(events.events.begin(), events.events.end(), [](const btrfsbackup::BackupRunEvent& event) {
        return event.kind == btrfsbackup::BackupRunEventKind::ActionFailed;
    });
    test_helpers::expect_true("hook timeout failed event", failed != events.events.end(), "missing failed action event");
    test_helpers::expect_eq("hook timeout event code", btrfsbackup::error_code_name(*failed->error_code), "hook.before_snapshot_timeout");
}

void test_hook_cancellation_finishes_run_as_cancelled() {
    RecordingActionHandler handler;
    handler.should_throw = true;
    handler.operation_cancelled = true;
    handler.throw_on = btrfsbackup::BackupRunActionKind::AfterSnapshotHook;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::AfterSnapshotHook),
    });

    btrfsbackup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);
    test_helpers::expect_true("hook cancellation result", result.cancelled, "run should be cancelled");
    test_helpers::expect_true("hook cancellation event", events.has_event(btrfsbackup::BackupRunEventKind::RunCancelled), "missing cancellation event");
    test_helpers::expect_true("hook cancellation no failure", !events.has_event(btrfsbackup::BackupRunEventKind::ActionFailed), "cancelled hook must not be reported as failed");
}

void test_remote_retention_failure_keeps_commit_checkpoint() {
    RecordingActionHandler handler;
    handler.should_throw = true;
    handler.throw_on = btrfsbackup::BackupRunActionKind::ApplyRemoteRetention;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::CreateSnapshot),
        action(btrfsbackup::BackupRunActionKind::SendReceive),
        action(btrfsbackup::BackupRunActionKind::VerifyReceived),
        action(btrfsbackup::BackupRunActionKind::CommitReceived),
        action(btrfsbackup::BackupRunActionKind::ApplyRemoteRetention),
        action(btrfsbackup::BackupRunActionKind::ApplyLocalRetention),
    });

    test_helpers::expect_validation_error("remote retention failure", [&] {
        (void)executor.execute(plan, events, cancellation);
    }, "injected action failure");
    test_helpers::expect_eq("remote retention checkpoint count", std::to_string(checkpoints.checkpoints.size()), "4");
    test_helpers::expect_eq("remote retention last checkpoint", action_name(checkpoints.checkpoints.back().action_kind), action_name(btrfsbackup::BackupRunActionKind::CommitReceived));
    test_helpers::expect_true("remote retention failed event", events.has_event(btrfsbackup::BackupRunEventKind::ActionFailed), "missing failed action event");
}

void test_local_retention_failure_keeps_remote_retention_checkpoint() {
    RecordingActionHandler handler;
    handler.should_throw = true;
    handler.throw_on = btrfsbackup::BackupRunActionKind::ApplyLocalRetention;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::BackupRunExecutor executor(handler, async_transfers, checkpoints);

    btrfsbackup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::BackupRunActionKind::CreateSnapshot),
        action(btrfsbackup::BackupRunActionKind::SendReceive),
        action(btrfsbackup::BackupRunActionKind::VerifyReceived),
        action(btrfsbackup::BackupRunActionKind::CommitReceived),
        action(btrfsbackup::BackupRunActionKind::ApplyRemoteRetention),
        action(btrfsbackup::BackupRunActionKind::ApplyLocalRetention),
    });

    test_helpers::expect_validation_error("local retention failure", [&] {
        (void)executor.execute(plan, events, cancellation);
    }, "injected action failure");
    test_helpers::expect_eq("local retention checkpoint count", std::to_string(checkpoints.checkpoints.size()), "5");
    test_helpers::expect_eq("local retention last checkpoint", action_name(checkpoints.checkpoints.back().action_kind), action_name(btrfsbackup::BackupRunActionKind::ApplyRemoteRetention));
    test_helpers::expect_true("local retention failed event", events.has_event(btrfsbackup::BackupRunEventKind::ActionFailed), "missing failed action event");
}

} // namespace

int main() {
    test_full_backup_flow_without_parent();
    test_executes_actions_and_writes_durable_checkpoints();
    test_pending_recovery_runs_before_source_cleanup();
    test_send_receive_delegates_to_transfer_pipeline();
    test_transfer_plan_estimates_snapshot_bytes();
    test_multi_source_progress_accumulates_run_bytes();
    test_cancels_between_actions();
    test_cancels_during_transfer_without_checkpointing_transfer();
    test_transfer_failure_emits_failed_action();
    test_receive_failure_is_reported_separately();
    test_commit_failure_after_successful_transfer_keeps_verify_checkpoint();
    test_commit_cleanup_failure_emits_recovery_required_code();
    test_hook_timeout_emits_stable_error_code();
    test_hook_cancellation_finishes_run_as_cancelled();
    test_remote_retention_failure_keeps_commit_checkpoint();
    test_local_retention_failure_keeps_remote_retention_checkpoint();

    return test_helpers::finish("backup run executor tests");
}
