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
#include <core/errors.hpp>

#include "support/fake_safe_directory.hpp"
#include "support/validation_test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

std::string action_name(btrfsbackup::backup::BackupRunActionKind kind) {
    return std::to_string(static_cast<int>(kind));
}

class RecordingActionHandler final : public btrfsbackup::backup::IBackupRunActionHandler {
  public:
    std::vector<std::string> calls;
    bool should_throw = false;
    bool recovery_required = false;
    bool operation_cancelled = false;
    std::string coded_error_code;
    btrfsbackup::backup::BackupRunActionKind throw_on = btrfsbackup::backup::BackupRunActionKind::CleanupSource;

    void handle(
        const btrfsbackup::backup::BackupRunAction& action,
        const btrfsbackup::backup::BackupRunPlan&,
        btrfsbackup::CancellationToken&
    ) override {
        const btrfsbackup::backup::BackupRunActionKind kind = btrfsbackup::backup::backup_run_action_kind(action);
        calls.push_back(
            std::string(btrfsbackup::backup::backup_run_action_source_id(action).value()) + ":" + action_name(kind)
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
                throw btrfsbackup::CodedOperationError(coded_error_code, "coded action failure");
            }
            throw btrfsbackup::ValidationError("injected action failure: " + action_name(kind));
        }
    }
};

class RecordingCheckpoints final : public btrfsbackup::backup::IBackupRunCheckpointStore {
  public:
    std::vector<btrfsbackup::backup::BackupRunCheckpoint> checkpoints;

    void write_checkpoint(const btrfsbackup::backup::BackupRunCheckpoint& checkpoint) override {
        checkpoints.push_back(checkpoint);
    }
};

class RecordingEvents final : public btrfsbackup::backup::IBackupRunEventSink {
  public:
    std::vector<btrfsbackup::backup::BackupRunEvent> events;
    btrfsbackup::CancellationToken* cancel_after_first_completed = nullptr;
    bool cancelled = false;

    void on_backup_run_event(const btrfsbackup::backup::BackupRunEvent& event) override {
        events.push_back(event);
        if (!cancelled && cancel_after_first_completed != nullptr && event.kind == btrfsbackup::backup::BackupRunEventKind::ActionCompleted) {
            cancelled = true;
            cancel_after_first_completed->request_cancel();
        }
    }

    bool has_event(btrfsbackup::backup::BackupRunEventKind kind) const {
        return std::any_of(events.begin(), events.end(), [kind](const btrfsbackup::backup::BackupRunEvent& event) {
            return event.kind == kind;
        });
    }
};

class RecordingTransferPipeline final : public btrfsbackup::backup::transfer::ITransferPipeline {
  public:
    std::vector<btrfsbackup::backup::transfer::TransferPipelinePlan> plans;
    btrfsbackup::backup::transfer::TransferResult next_result{
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

    btrfsbackup::backup::transfer::TransferResult run(
        const btrfsbackup::backup::transfer::TransferPipelinePlan& plan,
        btrfsbackup::backup::transfer::ITransferEventSink& events,
        btrfsbackup::CancellationToken&
    ) override {
        plans.push_back(plan);
        std::uint64_t reported_progress_bytes = progress_bytes;
        if (!progress_bytes_by_run.empty() && plans.size() <= progress_bytes_by_run.size()) {
            reported_progress_bytes = progress_bytes_by_run.at(plans.size() - 1);
        }
        if (reported_progress_bytes > 0) {
            events.on_transfer_event({
                .kind = btrfsbackup::backup::transfer::TransferEventKind::Progress,
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
        btrfsbackup::backup::transfer::TransferResult result = next_result;
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

test_support::FakeSafeDirectoryRootFactory safe_directories;

btrfsbackup::backup::BackupRunAction action(btrfsbackup::backup::BackupRunActionKind kind) {
    using btrfsbackup::RunId;
    using btrfsbackup::SourceId;
    const SourceId source_id{"root"};
    const fs::path local_snapshot = "/.snapshots/root/root-2026-08-23T080000Z";
    const fs::path incoming = "/mnt/backup/.incoming/root/run-1";
    switch (kind) {
    case btrfsbackup::backup::BackupRunActionKind::RecoverPending:
        return btrfsbackup::backup::RecoverPendingAction{source_id, btrfsbackup::backup::PendingRecoveryPlan{}};
    case btrfsbackup::backup::BackupRunActionKind::CleanupIncoming:
        return btrfsbackup::backup::CleanupIncomingAction{source_id, incoming.parent_path()};
    case btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook:
        return btrfsbackup::backup::RunHookAction{
            source_id,
            btrfsbackup::backup::HookPhase::BeforeSnapshot,
            btrfsbackup::config::ProfileHookCommand{"hook", {}, std::chrono::seconds{30}}
        };
    case btrfsbackup::backup::BackupRunActionKind::CreateSnapshot:
        return btrfsbackup::backup::CreateSnapshotAction{
            source_id,
            "/source",
            local_snapshot.parent_path(),
            local_snapshot,
            "/mnt/backup/root/snapshot",
            "/state",
            RunId{"run-1"},
        };
    case btrfsbackup::backup::BackupRunActionKind::AfterSnapshotHook:
        return btrfsbackup::backup::RunHookAction{
            source_id,
            btrfsbackup::backup::HookPhase::AfterSnapshot,
            btrfsbackup::config::ProfileHookCommand{"hook", {}, std::chrono::seconds{30}}
        };
    case btrfsbackup::backup::BackupRunActionKind::SendReceive:
        return btrfsbackup::backup::SendReceiveAction{source_id, local_snapshot, std::nullopt, "/mnt/backup/root", incoming};
    case btrfsbackup::backup::BackupRunActionKind::VerifyReceived:
        return btrfsbackup::backup::VerifyReceivedAction{source_id, local_snapshot, incoming / local_snapshot.filename()};
    case btrfsbackup::backup::BackupRunActionKind::CommitReceived:
        return btrfsbackup::backup::CommitReceivedAction{
            source_id,
            local_snapshot,
            incoming / local_snapshot.filename(),
            "/mnt/backup/root/snapshot",
        };
    case btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention:
        return btrfsbackup::backup::ApplyRemoteRetentionAction{source_id, btrfsbackup::backup::RetentionPlan{}};
    case btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention:
        return btrfsbackup::backup::ApplyLocalRetentionAction{source_id, btrfsbackup::backup::RetentionPlan{}};
    case btrfsbackup::backup::BackupRunActionKind::CleanupSource:
        return btrfsbackup::backup::CleanupSourceAction{source_id, incoming / local_snapshot.filename(), incoming, "/state/pending", "/state"};
    }
    throw std::logic_error("unsupported action kind");
}

btrfsbackup::backup::BackupRunPlan plan_with_actions(std::vector<btrfsbackup::backup::BackupRunAction> actions) {
    btrfsbackup::backup::BackupSourceRunPlan source{.source_id = btrfsbackup::SourceId{"root"}};
    source.local_snapshot_path = "/.snapshots/root/root-2026-08-23T080000Z";
    source.incoming_run_dir = "/mnt/backup/.incoming/root/run-1";
    source.actions = std::move(actions);

    return btrfsbackup::backup::BackupRunPlan{
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
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::CleanupIncoming),
        action(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot),
        action(btrfsbackup::backup::BackupRunActionKind::SendReceive),
        action(btrfsbackup::backup::BackupRunActionKind::VerifyReceived),
        action(btrfsbackup::backup::BackupRunActionKind::CommitReceived),
        action(btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention),
        action(btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention),
        action(btrfsbackup::backup::BackupRunActionKind::CleanupSource),
    });

    btrfsbackup::backup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("full flow completed", result.outcome == btrfsbackup::backup::BackupRunExecutionOutcome::Completed, "run should complete");
    test_helpers::expect_eq("full flow actions", std::to_string(result.actions_completed), "8");
    test_helpers::expect_eq("full flow transfer count", std::to_string(transfers.plans.size()), "1");
    const std::vector<std::string>& send_argv = transfers.plans.at(0).producer_argv;
    test_helpers::expect_eq("full send argc", std::to_string(send_argv.size()), "6");
    test_helpers::expect_eq("full send binary", send_argv.at(0), "btrfs");
    test_helpers::expect_eq("full send subcommand", send_argv.at(1), "send");
    test_helpers::expect_eq("full send protocol flag", send_argv.at(2), "--proto");
    test_helpers::expect_eq("full send protocol", send_argv.at(3), "2");
    test_helpers::expect_eq("full send compressed data", send_argv.at(4), "--compressed-data");
    test_helpers::expect_eq("full send snapshot", send_argv.at(5), "/.snapshots/root/root-2026-08-23T080000Z");
    test_helpers::expect_eq("full effect count", std::to_string(handler.calls.size()), "7");
    test_helpers::expect_eq("full checkpoint count", std::to_string(checkpoints.checkpoints.size()), "8");
    test_helpers::expect_eq("full last checkpoint", action_name(checkpoints.checkpoints.back().action_kind), action_name(btrfsbackup::backup::BackupRunActionKind::CleanupSource));
}

void test_executes_actions_and_writes_durable_checkpoints() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::CleanupIncoming),
        action(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot),
    });

    btrfsbackup::backup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("executor completed", result.outcome == btrfsbackup::backup::BackupRunExecutionOutcome::Completed, "run should complete");
    test_helpers::expect_eq("actions completed", std::to_string(result.actions_completed), "2");
    test_helpers::expect_eq("effect count", std::to_string(handler.calls.size()), "2");
    test_helpers::expect_eq("first effect", handler.calls.at(0), "root:" + action_name(btrfsbackup::backup::BackupRunActionKind::CleanupIncoming));
    test_helpers::expect_eq("second effect", handler.calls.at(1), "root:" + action_name(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot));
    test_helpers::expect_eq("checkpoint count", std::to_string(checkpoints.checkpoints.size()), "2");
    test_helpers::expect_eq("first checkpoint", action_name(checkpoints.checkpoints.at(0).action_kind), action_name(btrfsbackup::backup::BackupRunActionKind::CleanupIncoming));
    test_helpers::expect_eq("second checkpoint", action_name(checkpoints.checkpoints.at(1).action_kind), action_name(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot));
    test_helpers::expect_true("run completed event", events.has_event(btrfsbackup::backup::BackupRunEventKind::RunCompleted), "missing completion event");
}

void test_pending_recovery_runs_before_source_cleanup() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::RecoverPending),
        action(btrfsbackup::backup::BackupRunActionKind::CleanupIncoming),
        action(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot),
    });

    btrfsbackup::backup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("pending recovery completed", result.outcome == btrfsbackup::backup::BackupRunExecutionOutcome::Completed, "run should complete");
    test_helpers::expect_eq("pending recovery first effect", handler.calls.at(0), "root:" + action_name(btrfsbackup::backup::BackupRunActionKind::RecoverPending));
    test_helpers::expect_eq("pending recovery second effect", handler.calls.at(1), "root:" + action_name(btrfsbackup::backup::BackupRunActionKind::CleanupIncoming));
    test_helpers::expect_eq("pending recovery checkpoint count", std::to_string(checkpoints.checkpoints.size()), "3");
    test_helpers::expect_eq("pending recovery first checkpoint", action_name(checkpoints.checkpoints.at(0).action_kind), action_name(btrfsbackup::backup::BackupRunActionKind::RecoverPending));
}

void test_send_receive_delegates_to_transfer_pipeline() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    transfers.progress_bytes = 8192;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        btrfsbackup::backup::SendReceiveAction{
            btrfsbackup::SourceId{"root"},
            "/.snapshots/root/root-2026-08-23T080000Z",
            fs::path{"/.snapshots/root/root-2026-08-22T080000Z"},
            "/mnt/backup/root",
            "/mnt/backup/.incoming/root/run-1",
        },
    });

    btrfsbackup::backup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("transfer completed", result.outcome == btrfsbackup::backup::BackupRunExecutionOutcome::Completed, "run should complete");
    test_helpers::expect_true("transfer bypasses effect handler", handler.calls.empty(), "transfer must be coordinated as one operation");
    test_helpers::expect_eq("transfer count", std::to_string(transfers.plans.size()), "1");
    const btrfsbackup::backup::transfer::TransferPipelinePlan& transfer_plan = transfers.plans.at(0);
    test_helpers::expect_eq("send binary", transfer_plan.producer_argv.at(0), "btrfs");
    test_helpers::expect_eq("send subcommand", transfer_plan.producer_argv.at(1), "send");
    test_helpers::expect_eq("send protocol flag", transfer_plan.producer_argv.at(2), "--proto");
    test_helpers::expect_eq("send protocol", transfer_plan.producer_argv.at(3), "2");
    test_helpers::expect_eq("send compressed data", transfer_plan.producer_argv.at(4), "--compressed-data");
    test_helpers::expect_eq("send parent flag", transfer_plan.producer_argv.at(5), "-p");
    test_helpers::expect_eq("send parent", transfer_plan.producer_argv.at(6), "/.snapshots/root/root-2026-08-22T080000Z");
    test_helpers::expect_eq("receive dir", transfer_plan.consumer_argv.at(2), "/mnt/backup/.incoming/root/run-1");
    test_helpers::expect_eq("transfer checkpoint count", std::to_string(checkpoints.checkpoints.size()), "1");

    auto progress = std::find_if(events.events.begin(), events.events.end(), [](const btrfsbackup::backup::BackupRunEvent& event) {
        return event.kind == btrfsbackup::backup::BackupRunEventKind::TransferProgress;
    });
    test_helpers::expect_true("progress event", progress != events.events.end(), "missing transfer progress event");
    test_helpers::expect_eq("progress bytes", std::to_string(progress->bytes_transferred), "8192");
    test_helpers::expect_eq("progress delta", std::to_string(progress->delta_bytes), "8192");
}

void test_transfer_paths_are_pinned_through_injected_factory() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    test_support::FakeSafeDirectoryRootFactory pinned_directories{"/pinned"};
    btrfsbackup::backup::BackupRunExecutor executor(
        handler,
        async_transfers,
        checkpoints,
        pinned_directories
    );

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        btrfsbackup::backup::SendReceiveAction{
            btrfsbackup::SourceId{"root"},
            "/.snapshots/root/current",
            fs::path{"/.snapshots/root/parent"},
            "/mnt/backup/root",
            "/mnt/backup/.incoming/root/run-1",
        },
    });
    plan.target_mount_point = "/mnt/backup";

    (void)executor.execute(plan, events, cancellation);

    const btrfsbackup::backup::transfer::TransferPipelinePlan& transfer_plan = transfers.plans.at(0);
    test_helpers::expect_eq("pinned snapshot", transfer_plan.producer_argv.at(7), "/pinned/.snapshots/root/current");
    test_helpers::expect_eq("pinned parent", transfer_plan.producer_argv.at(6), "/pinned/.snapshots/root/parent");
    test_helpers::expect_eq("pinned receive", transfer_plan.consumer_argv.at(2), "/pinned/mnt/backup/.incoming/root/run-1");
    test_helpers::expect_eq("retained safe handles", std::to_string(transfer_plan.retained_resources.size()), "3");
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
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        btrfsbackup::backup::SendReceiveAction{
            btrfsbackup::SourceId{"root"},
            root / ".snapshots" / "root",
            std::nullopt,
            "/mnt/backup/root",
            "/mnt/backup/.incoming/root/run-1",
        },
    });

    btrfsbackup::backup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("estimate run completed", result.outcome == btrfsbackup::backup::BackupRunExecutionOutcome::Completed, "run should complete");
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
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupSourceRunPlan home{.source_id = btrfsbackup::SourceId{"home"}};
    home.local_snapshot_path = "/.snapshots/home/home-2026-08-23T080000Z";
    home.incoming_run_dir = "/mnt/backup/.incoming/home/run-1";
    home.actions = {
        btrfsbackup::backup::SendReceiveAction{
            home.source_id,
            home.local_snapshot_path,
            std::nullopt,
            "/mnt/backup/home",
            home.incoming_run_dir,
        },
    };

    btrfsbackup::backup::BackupSourceRunPlan root{.source_id = btrfsbackup::SourceId{"root"}};
    root.local_snapshot_path = "/.snapshots/root/root-2026-08-23T080000Z";
    root.incoming_run_dir = "/mnt/backup/.incoming/root/run-1";
    root.actions = {
        btrfsbackup::backup::SendReceiveAction{
            root.source_id,
            root.local_snapshot_path,
            std::nullopt,
            "/mnt/backup/root",
            root.incoming_run_dir,
        },
    };

    btrfsbackup::backup::BackupRunPlan plan{
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"run-1"},
        .sources = {home, root},
    };

    btrfsbackup::backup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("multi progress completed", result.outcome == btrfsbackup::backup::BackupRunExecutionOutcome::Completed, "run should complete");
    std::vector<btrfsbackup::backup::BackupRunEvent> progress_events;
    std::copy_if(events.events.begin(), events.events.end(), std::back_inserter(progress_events), [](const btrfsbackup::backup::BackupRunEvent& event) {
        return event.kind == btrfsbackup::backup::BackupRunEventKind::TransferProgress;
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
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::CleanupIncoming),
        action(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot),
    });

    btrfsbackup::backup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("run cancelled", result.outcome == btrfsbackup::backup::BackupRunExecutionOutcome::Cancelled, "run should be cancelled");
    test_helpers::expect_eq("actions completed before cancel", std::to_string(result.actions_completed), "1");
    test_helpers::expect_eq("effect count before cancel", std::to_string(handler.calls.size()), "1");
    test_helpers::expect_eq("checkpoint count before cancel", std::to_string(checkpoints.checkpoints.size()), "1");
    test_helpers::expect_true("cancel event", events.has_event(btrfsbackup::backup::BackupRunEventKind::RunCancelled), "missing cancel event");
}

void test_cancels_during_transfer_without_checkpointing_transfer() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    transfers.cancel_during_run = true;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot),
        action(btrfsbackup::backup::BackupRunActionKind::SendReceive),
        action(btrfsbackup::backup::BackupRunActionKind::VerifyReceived),
    });

    btrfsbackup::backup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("transfer cancellation result", result.outcome == btrfsbackup::backup::BackupRunExecutionOutcome::Cancelled, "run should be cancelled");
    test_helpers::expect_eq("transfer cancellation completed actions", std::to_string(result.actions_completed), "1");
    test_helpers::expect_eq("transfer cancellation checkpoint count", std::to_string(checkpoints.checkpoints.size()), "1");
    test_helpers::expect_eq("transfer cancellation last checkpoint", action_name(checkpoints.checkpoints.back().action_kind), action_name(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot));
    test_helpers::expect_true("transfer cancellation event", events.has_event(btrfsbackup::backup::BackupRunEventKind::RunCancelled), "missing cancel event");
}

void test_transfer_failure_emits_failed_action() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    transfers.next_result.producer.exit_code = 7;
    transfers.next_result.producer.diagnostics = "send failed";
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::SendReceive),
    });

    test_helpers::expect_validation_error("transfer failure", [&] { (void)executor.execute(plan, events, cancellation); }, "producer failed with exit code 7");
    test_helpers::expect_eq("failed checkpoint count", std::to_string(checkpoints.checkpoints.size()), "0");
    test_helpers::expect_true("failed action event", events.has_event(btrfsbackup::backup::BackupRunEventKind::ActionFailed), "missing failed action event");
    auto failed = std::find_if(events.events.begin(), events.events.end(), [](const btrfsbackup::backup::BackupRunEvent& event) {
        return event.kind == btrfsbackup::backup::BackupRunEventKind::ActionFailed;
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
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::SendReceive),
    });

    test_helpers::expect_validation_error("receive failure", [&] { (void)executor.execute(plan, events, cancellation); }, "consumer failed with exit code 9");
    test_helpers::expect_eq("receive failure checkpoint count", std::to_string(checkpoints.checkpoints.size()), "0");
    auto failed = std::find_if(events.events.begin(), events.events.end(), [](const btrfsbackup::backup::BackupRunEvent& event) {
        return event.kind == btrfsbackup::backup::BackupRunEventKind::ActionFailed;
    });
    test_helpers::expect_true("receive failed action event", failed != events.events.end(), "missing failed action event");
    test_helpers::expect_eq("receive failed error code", btrfsbackup::error_code_name(*failed->error_code), "transfer.consumer_failed");
    test_helpers::expect_contains("receive failed message", failed->message, "consumer failed with exit code 9");
}

void test_commit_failure_after_successful_transfer_keeps_verify_checkpoint() {
    RecordingActionHandler handler;
    handler.should_throw = true;
    handler.throw_on = btrfsbackup::backup::BackupRunActionKind::CommitReceived;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot),
        action(btrfsbackup::backup::BackupRunActionKind::SendReceive),
        action(btrfsbackup::backup::BackupRunActionKind::VerifyReceived),
        action(btrfsbackup::backup::BackupRunActionKind::CommitReceived),
    });

    test_helpers::expect_validation_error("commit failure", [&] { (void)executor.execute(plan, events, cancellation); }, "injected action failure");
    test_helpers::expect_eq("commit failure transfer count", std::to_string(transfers.plans.size()), "1");
    test_helpers::expect_eq("commit failure checkpoint count", std::to_string(checkpoints.checkpoints.size()), "3");
    test_helpers::expect_eq("commit failure last checkpoint", action_name(checkpoints.checkpoints.back().action_kind), action_name(btrfsbackup::backup::BackupRunActionKind::VerifyReceived));
    test_helpers::expect_true("commit failed action event", events.has_event(btrfsbackup::backup::BackupRunEventKind::ActionFailed), "missing failed action event");
}

void test_commit_cleanup_failure_emits_recovery_required_code() {
    RecordingActionHandler handler;
    handler.should_throw = true;
    handler.recovery_required = true;
    handler.throw_on = btrfsbackup::backup::BackupRunActionKind::CommitReceived;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::CommitReceived),
    });

    try {
        (void)executor.execute(plan, events, cancellation);
        test_helpers::expect_true("commit cleanup failure type", false, "recovery requirement should fail the action");
    } catch (const btrfsbackup::RecoveryRequiredError& error) {
        test_helpers::expect_contains("commit cleanup failure message", error.what(), "repository requires recovery");
    }
    auto failed = std::find_if(events.events.begin(), events.events.end(), [](const btrfsbackup::backup::BackupRunEvent& event) {
        return event.kind == btrfsbackup::backup::BackupRunEventKind::ActionFailed;
    });
    test_helpers::expect_true("commit cleanup failed event", failed != events.events.end(), "missing failed action event");
    test_helpers::expect_eq("commit cleanup error code", btrfsbackup::error_code_name(*failed->error_code), "repository.recovery_required");
}

void test_hook_timeout_emits_stable_error_code() {
    RecordingActionHandler handler;
    handler.should_throw = true;
    handler.throw_on = btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook;
    handler.coded_error_code = "hook.before_snapshot_timeout";
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook),
    });

    try {
        (void)executor.execute(plan, events, cancellation);
        test_helpers::expect_true("hook timeout type", false, "hook timeout should fail the action");
    } catch (const btrfsbackup::CodedOperationError& error) {
        test_helpers::expect_contains("hook timeout message", error.what(), "coded action failure");
    }
    auto failed = std::find_if(events.events.begin(), events.events.end(), [](const btrfsbackup::backup::BackupRunEvent& event) {
        return event.kind == btrfsbackup::backup::BackupRunEventKind::ActionFailed;
    });
    test_helpers::expect_true("hook timeout failed event", failed != events.events.end(), "missing failed action event");
    test_helpers::expect_eq("hook timeout event code", btrfsbackup::error_code_name(*failed->error_code), "hook.before_snapshot_timeout");
}

void test_hook_cancellation_finishes_run_as_cancelled() {
    RecordingActionHandler handler;
    handler.should_throw = true;
    handler.operation_cancelled = true;
    handler.throw_on = btrfsbackup::backup::BackupRunActionKind::AfterSnapshotHook;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::AfterSnapshotHook),
    });

    btrfsbackup::backup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);
    test_helpers::expect_true("hook cancellation result", result.outcome == btrfsbackup::backup::BackupRunExecutionOutcome::Cancelled, "run should be cancelled");
    test_helpers::expect_true("hook cancellation event", events.has_event(btrfsbackup::backup::BackupRunEventKind::RunCancelled), "missing cancellation event");
    test_helpers::expect_true("hook cancellation no failure", !events.has_event(btrfsbackup::backup::BackupRunEventKind::ActionFailed), "cancelled hook must not be reported as failed");
}

void test_remote_retention_failure_keeps_commit_checkpoint() {
    RecordingActionHandler handler;
    handler.should_throw = true;
    handler.throw_on = btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot),
        action(btrfsbackup::backup::BackupRunActionKind::SendReceive),
        action(btrfsbackup::backup::BackupRunActionKind::VerifyReceived),
        action(btrfsbackup::backup::BackupRunActionKind::CommitReceived),
        action(btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention),
        action(btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention),
    });

    test_helpers::expect_validation_error("remote retention failure", [&] { (void)executor.execute(plan, events, cancellation); }, "injected action failure");
    test_helpers::expect_eq("remote retention checkpoint count", std::to_string(checkpoints.checkpoints.size()), "4");
    test_helpers::expect_eq("remote retention last checkpoint", action_name(checkpoints.checkpoints.back().action_kind), action_name(btrfsbackup::backup::BackupRunActionKind::CommitReceived));
    test_helpers::expect_true("remote retention failed event", events.has_event(btrfsbackup::backup::BackupRunEventKind::ActionFailed), "missing failed action event");
}

void test_local_retention_failure_keeps_remote_retention_checkpoint() {
    RecordingActionHandler handler;
    handler.should_throw = true;
    handler.throw_on = btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot),
        action(btrfsbackup::backup::BackupRunActionKind::SendReceive),
        action(btrfsbackup::backup::BackupRunActionKind::VerifyReceived),
        action(btrfsbackup::backup::BackupRunActionKind::CommitReceived),
        action(btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention),
        action(btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention),
    });

    test_helpers::expect_validation_error("local retention failure", [&] { (void)executor.execute(plan, events, cancellation); }, "injected action failure");
    test_helpers::expect_eq("local retention checkpoint count", std::to_string(checkpoints.checkpoints.size()), "5");
    test_helpers::expect_eq("local retention last checkpoint", action_name(checkpoints.checkpoints.back().action_kind), action_name(btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention));
    test_helpers::expect_true("local retention failed event", events.has_event(btrfsbackup::backup::BackupRunEventKind::ActionFailed), "missing failed action event");
}

} // namespace

int main() {
    test_full_backup_flow_without_parent();
    test_executes_actions_and_writes_durable_checkpoints();
    test_pending_recovery_runs_before_source_cleanup();
    test_send_receive_delegates_to_transfer_pipeline();
    test_transfer_paths_are_pinned_through_injected_factory();
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
