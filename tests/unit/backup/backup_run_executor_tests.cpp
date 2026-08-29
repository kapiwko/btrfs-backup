// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
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

std::string event_action_name(const btrfsbackup::backup::BackupRunEvent& event) {
    const auto action_kind = btrfsbackup::backup::backup_run_event_action_kind(event);
    return action_kind.has_value() ? action_name(*action_kind) : "none";
}

template <typename Event>
const Event* find_event(const std::vector<btrfsbackup::backup::BackupRunEvent>& events) {
    for (const auto& event : events) {
        if (const auto* typed_event = std::get_if<Event>(&event)) {
            return typed_event;
        }
    }
    return nullptr;
}

bool run_completed(const btrfsbackup::backup::BackupRunExecutionResult& result) {
    return std::holds_alternative<btrfsbackup::backup::BackupRunExecutionCompleted>(result);
}

const btrfsbackup::backup::BackupRunExecutionFailed* failed_run(
    const btrfsbackup::backup::BackupRunExecutionResult& result
) {
    return std::get_if<btrfsbackup::backup::BackupRunExecutionFailed>(&result);
}

std::size_t actions_completed(const btrfsbackup::backup::BackupRunExecutionResult& result) {
    return std::visit([](const auto& outcome) {
        return outcome.actions_completed;
    },
                      result);
}

class RecordingActionHandler final : public btrfsbackup::backup::IBackupRunActionHandler {
  public:
    std::vector<std::string> calls;
    std::vector<std::string>* execution_trace = nullptr;
    bool should_throw = false;
    bool recovery_required = false;
    bool operation_cancelled = false;
    std::optional<btrfsbackup::ErrorCode> coded_error_code;
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
        if (execution_trace != nullptr) {
            execution_trace->push_back("effect:" + action_name(kind));
        }
        if (should_throw && kind == throw_on) {
            if (operation_cancelled) {
                throw btrfsbackup::OperationCancelledError("hook cancelled");
            }
            if (recovery_required) {
                throw btrfsbackup::RecoveryRequiredError(
                    btrfsbackup::ErrorCode::RepositoryRecoveryRequired,
                    "commit verification failed; cleanup failed; repository requires recovery"
                );
            }
            if (coded_error_code.has_value()) {
                throw btrfsbackup::CodedOperationError(*coded_error_code, "coded action failure");
            }
            throw btrfsbackup::ValidationError("injected action failure: " + action_name(kind));
        }
    }
};

class RecordingCheckpoints final : public btrfsbackup::backup::IBackupRunCheckpointStore {
  public:
    std::vector<btrfsbackup::backup::BackupRunCheckpoint> checkpoints;
    std::vector<std::string>* execution_trace = nullptr;

    void write_checkpoint(const btrfsbackup::backup::BackupRunCheckpoint& checkpoint) override {
        checkpoints.push_back(checkpoint);
        if (execution_trace != nullptr) {
            execution_trace->push_back("checkpoint-store:" + action_name(checkpoint.action_kind));
        }
    }
};

class RecordingEvents final : public btrfsbackup::backup::IBackupRunEventSink {
  public:
    std::vector<btrfsbackup::backup::BackupRunEvent> events;
    std::vector<std::string>* execution_trace = nullptr;
    btrfsbackup::CancellationToken* cancel_after_first_completed = nullptr;
    bool cancelled = false;

    void on_backup_run_event(const btrfsbackup::backup::BackupRunEvent& event) override {
        events.push_back(event);
        const auto kind = btrfsbackup::backup::backup_run_event_kind(event);
        if (execution_trace != nullptr) {
            if (kind == btrfsbackup::backup::BackupRunEventKind::ActionStarted) {
                execution_trace->push_back("action-started:" + event_action_name(event));
            } else if (kind == btrfsbackup::backup::BackupRunEventKind::ActionCompleted) {
                execution_trace->push_back("action-completed:" + event_action_name(event));
            } else if (kind == btrfsbackup::backup::BackupRunEventKind::CheckpointWritten) {
                execution_trace->push_back("checkpoint-written:" + event_action_name(event));
            }
        }
        if (!cancelled && cancel_after_first_completed != nullptr && kind == btrfsbackup::backup::BackupRunEventKind::ActionCompleted) {
            cancelled = true;
            cancel_after_first_completed->request_cancel();
        }
    }

    bool has_event(btrfsbackup::backup::BackupRunEventKind kind) const {
        return std::any_of(events.begin(), events.end(), [kind](const btrfsbackup::backup::BackupRunEvent& event) {
            return btrfsbackup::backup::backup_run_event_kind(event) == kind;
        });
    }
};

class RecordingTransferPipeline final : public btrfsbackup::backup::transfer::ITransferPipeline {
  public:
    std::vector<btrfsbackup::backup::transfer::TransferPipelinePlan> plans;
    std::vector<std::string>* execution_trace = nullptr;
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
        if (execution_trace != nullptr) {
            execution_trace->push_back(
                "effect:" + action_name(btrfsbackup::backup::BackupRunActionKind::SendReceive)
            );
        }
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
        return btrfsbackup::backup::ApplyRemoteRetentionAction{
            source_id,
            btrfsbackup::backup::RetentionPlan{.source_id = source_id}
        };
    case btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention:
        return btrfsbackup::backup::ApplyLocalRetentionAction{
            source_id,
            btrfsbackup::backup::RetentionPlan{.source_id = source_id}
        };
    case btrfsbackup::backup::BackupRunActionKind::CleanupSource:
        return btrfsbackup::backup::CleanupSourceAction{source_id, incoming / local_snapshot.filename(), incoming, "/state/pending", "/state"};
    }
    throw std::logic_error("unsupported action kind");
}

btrfsbackup::backup::BackupRunPlan plan_with_actions(std::vector<btrfsbackup::backup::BackupRunAction> actions) {
    btrfsbackup::backup::BackupSourceRunPlan source{
        btrfsbackup::SourceId{"root"},
        std::move(actions),
    };

    return btrfsbackup::backup::BackupRunPlan{
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"run-1"},
        .sources = {source},
    };
}

void test_lifecycle_events_do_not_have_synthetic_actions() {
    RecordingActionHandler handler;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    (void)executor.execute(
        plan_with_actions({action(btrfsbackup::backup::BackupRunActionKind::CleanupIncoming)}),
        events,
        cancellation
    );

    const std::array lifecycle_kinds{
        btrfsbackup::backup::BackupRunEventKind::SourceStarted,
        btrfsbackup::backup::BackupRunEventKind::SourceCompleted,
    };
    for (const btrfsbackup::backup::BackupRunEventKind kind : lifecycle_kinds) {
        const auto found = std::find_if(
            events.events.begin(),
            events.events.end(),
            [kind](const btrfsbackup::backup::BackupRunEvent& event) {
                return btrfsbackup::backup::backup_run_event_kind(event) == kind;
            }
        );
        test_helpers::expect_true(
            "lifecycle event exists " + std::to_string(static_cast<int>(kind)),
            found != events.events.end(),
            "missing lifecycle event"
        );
        if (found == events.events.end()) {
            continue;
        }
        test_helpers::expect_true(
            "lifecycle action absent " + std::to_string(static_cast<int>(kind)),
            !btrfsbackup::backup::backup_run_event_action_kind(*found).has_value(),
            "lifecycle event contains a synthetic action"
        );
    }
}

void test_every_action_uses_uniform_execution_semantics() {
    constexpr std::array action_kinds{
        btrfsbackup::backup::BackupRunActionKind::RecoverPending,
        btrfsbackup::backup::BackupRunActionKind::CleanupIncoming,
        btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook,
        btrfsbackup::backup::BackupRunActionKind::CreateSnapshot,
        btrfsbackup::backup::BackupRunActionKind::AfterSnapshotHook,
        btrfsbackup::backup::BackupRunActionKind::SendReceive,
        btrfsbackup::backup::BackupRunActionKind::VerifyReceived,
        btrfsbackup::backup::BackupRunActionKind::CommitReceived,
        btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention,
        btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention,
        btrfsbackup::backup::BackupRunActionKind::CleanupSource,
    };
    static_assert(
        action_kinds.size() == static_cast<std::size_t>(btrfsbackup::backup::BackupRunActionKind::CleanupSource) + 1
    );

    for (const btrfsbackup::backup::BackupRunActionKind kind : action_kinds) {
        std::vector<std::string> execution_trace;
        RecordingActionHandler handler;
        handler.execution_trace = &execution_trace;
        RecordingTransferPipeline transfers;
        transfers.execution_trace = &execution_trace;
        RecordingCheckpoints checkpoints;
        checkpoints.execution_trace = &execution_trace;
        RecordingEvents events;
        events.execution_trace = &execution_trace;
        btrfsbackup::CancellationToken cancellation;
        btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
        btrfsbackup::backup::BackupRunExecutor executor(
            handler,
            async_transfers,
            checkpoints,
            safe_directories
        );

        btrfsbackup::backup::BackupRunExecutionResult result = executor.execute(
            plan_with_actions({action(kind)}),
            events,
            cancellation
        );

        const std::string kind_name = action_name(kind);
        test_helpers::expect_true(
            "uniform action outcome " + kind_name,
            run_completed(result),
            "action did not complete"
        );
        test_helpers::expect_eq(
            "uniform action count " + kind_name,
            std::to_string(actions_completed(result)),
            "1"
        );
        test_helpers::expect_eq(
            "uniform handler count " + kind_name,
            std::to_string(handler.calls.size()),
            kind == btrfsbackup::backup::BackupRunActionKind::SendReceive ? "0" : "1"
        );
        test_helpers::expect_eq(
            "uniform transfer count " + kind_name,
            std::to_string(transfers.plans.size()),
            kind == btrfsbackup::backup::BackupRunActionKind::SendReceive ? "1" : "0"
        );
        test_helpers::expect_eq(
            "uniform checkpoint count " + kind_name,
            std::to_string(checkpoints.checkpoints.size()),
            "1"
        );

        const std::array expected_trace{
            "action-started:" + kind_name,
            "effect:" + kind_name,
            "action-completed:" + kind_name,
            "checkpoint-store:" + kind_name,
            "checkpoint-written:" + kind_name,
        };
        test_helpers::expect_eq(
            "uniform trace size " + kind_name,
            std::to_string(execution_trace.size()),
            std::to_string(expected_trace.size())
        );
        for (std::size_t index = 0; index < expected_trace.size(); ++index) {
            test_helpers::expect_eq(
                "uniform trace entry " + kind_name + ":" + std::to_string(index),
                execution_trace.at(index),
                expected_trace.at(index)
            );
        }
    }
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

    test_helpers::expect_true("full flow completed", run_completed(result), "run should complete");
    test_helpers::expect_eq("full flow actions", std::to_string(actions_completed(result)), "8");
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

    test_helpers::expect_true("executor completed", run_completed(result), "run should complete");
    test_helpers::expect_eq("actions completed", std::to_string(actions_completed(result)), "2");
    test_helpers::expect_eq("effect count", std::to_string(handler.calls.size()), "2");
    test_helpers::expect_eq("first effect", handler.calls.at(0), "root:" + action_name(btrfsbackup::backup::BackupRunActionKind::CleanupIncoming));
    test_helpers::expect_eq("second effect", handler.calls.at(1), "root:" + action_name(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot));
    test_helpers::expect_eq("checkpoint count", std::to_string(checkpoints.checkpoints.size()), "2");
    test_helpers::expect_eq("first checkpoint", action_name(checkpoints.checkpoints.at(0).action_kind), action_name(btrfsbackup::backup::BackupRunActionKind::CleanupIncoming));
    test_helpers::expect_eq("second checkpoint", action_name(checkpoints.checkpoints.at(1).action_kind), action_name(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot));
    test_helpers::expect_true(
        "run completion owned by service",
        !events.has_event(btrfsbackup::backup::BackupRunEventKind::RunCompleted),
        "executor emitted a terminal success before service persistence"
    );
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

    test_helpers::expect_true("pending recovery completed", run_completed(result), "run should complete");
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

    test_helpers::expect_true("transfer completed", run_completed(result), "run should complete");
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
        return std::holds_alternative<btrfsbackup::backup::TransferProgress>(event);
    });
    test_helpers::expect_true("progress event", progress != events.events.end(), "missing transfer progress event");
    if (progress != events.events.end()) {
        const auto& transfer_progress = std::get<btrfsbackup::backup::TransferProgress>(*progress);
        test_helpers::expect_eq("progress bytes", std::to_string(transfer_progress.bytes_transferred), "8192");
        test_helpers::expect_eq("progress delta", std::to_string(transfer_progress.delta_bytes), "8192");
    }
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

    test_helpers::expect_true("estimate run completed", run_completed(result), "run should complete");
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

    const btrfsbackup::SourceId home_id{"home"};
    const fs::path home_snapshot = "/.snapshots/home/home-2026-08-23T080000Z";
    const fs::path home_incoming = "/mnt/backup/.incoming/home/run-1";
    btrfsbackup::backup::BackupSourceRunPlan home{
        home_id,
        {btrfsbackup::backup::SendReceiveAction{
            home_id,
            home_snapshot,
            std::nullopt,
            "/mnt/backup/home",
            home_incoming,
        }},
    };

    const btrfsbackup::SourceId root_id{"root"};
    const fs::path root_snapshot = "/.snapshots/root/root-2026-08-23T080000Z";
    const fs::path root_incoming = "/mnt/backup/.incoming/root/run-1";
    btrfsbackup::backup::BackupSourceRunPlan root{
        root_id,
        {btrfsbackup::backup::SendReceiveAction{
            root_id,
            root_snapshot,
            std::nullopt,
            "/mnt/backup/root",
            root_incoming,
        }},
    };

    btrfsbackup::backup::BackupRunPlan plan{
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"run-1"},
        .sources = {home, root},
    };

    btrfsbackup::backup::BackupRunExecutionResult result = executor.execute(plan, events, cancellation);

    test_helpers::expect_true("multi progress completed", run_completed(result), "run should complete");
    std::vector<btrfsbackup::backup::TransferProgress> progress_events;
    for (const auto& event : events.events) {
        if (const auto* progress = std::get_if<btrfsbackup::backup::TransferProgress>(&event)) {
            progress_events.push_back(*progress);
        }
    }
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

    test_helpers::expect_true("run cancelled", std::holds_alternative<btrfsbackup::backup::BackupRunExecutionCancelled>(result), "run should be cancelled");
    test_helpers::expect_eq("actions completed before cancel", std::to_string(actions_completed(result)), "1");
    test_helpers::expect_eq("effect count before cancel", std::to_string(handler.calls.size()), "1");
    test_helpers::expect_eq("checkpoint count before cancel", std::to_string(checkpoints.checkpoints.size()), "1");
    test_helpers::expect_true("cancel event", events.has_event(btrfsbackup::backup::BackupRunEventKind::RunCancelled), "missing cancel event");
    const auto cancelled = std::find_if(events.events.begin(), events.events.end(), [](const btrfsbackup::backup::BackupRunEvent& event) {
        return std::holds_alternative<btrfsbackup::backup::RunCancelled>(event);
    });
    test_helpers::expect_true(
        "cancel between actions has no action",
        cancelled != events.events.end() && !std::get<btrfsbackup::backup::RunCancelled>(*cancelled).action_kind.has_value(),
        "cancellation between actions contains a synthetic action"
    );
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

    test_helpers::expect_true("transfer cancellation result", std::holds_alternative<btrfsbackup::backup::BackupRunExecutionCancelled>(result), "run should be cancelled");
    test_helpers::expect_eq("transfer cancellation completed actions", std::to_string(actions_completed(result)), "1");
    test_helpers::expect_eq("transfer cancellation checkpoint count", std::to_string(checkpoints.checkpoints.size()), "1");
    test_helpers::expect_eq("transfer cancellation last checkpoint", action_name(checkpoints.checkpoints.back().action_kind), action_name(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot));
    test_helpers::expect_true("transfer cancellation event", events.has_event(btrfsbackup::backup::BackupRunEventKind::RunCancelled), "missing cancel event");
    const auto cancelled = std::find_if(events.events.begin(), events.events.end(), [](const btrfsbackup::backup::BackupRunEvent& event) {
        return std::holds_alternative<btrfsbackup::backup::RunCancelled>(event);
    });
    test_helpers::expect_true(
        "transfer cancellation action",
        cancelled != events.events.end() && std::get<btrfsbackup::backup::RunCancelled>(*cancelled).action_kind == btrfsbackup::backup::BackupRunActionKind::SendReceive,
        "transfer cancellation lost its active action"
    );
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

    const auto result = executor.execute(plan, events, cancellation);
    const auto* run_failure = failed_run(result);
    test_helpers::expect_true("transfer failure result", run_failure != nullptr, "run should return a typed failure");
    if (run_failure != nullptr) {
        test_helpers::expect_contains("transfer failure message", run_failure->error_message, "producer failed with exit code 7");
    }
    test_helpers::expect_eq("failed checkpoint count", std::to_string(checkpoints.checkpoints.size()), "0");
    test_helpers::expect_true("failed action event", events.has_event(btrfsbackup::backup::BackupRunEventKind::ActionFailed), "missing failed action event");
    const auto* failed = find_event<btrfsbackup::backup::ActionFailed>(events.events);
    test_helpers::expect_true("transfer failed action event", failed != nullptr, "missing failed action event");
    if (failed != nullptr) {
        test_helpers::expect_eq("transfer failed error code", btrfsbackup::error_code_name(*failed->error_code), "transfer.producer_failed");
    }
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

    const auto result = executor.execute(plan, events, cancellation);
    const auto* run_failure = failed_run(result);
    test_helpers::expect_true("receive failure result", run_failure != nullptr, "run should return a typed failure");
    if (run_failure != nullptr) {
        test_helpers::expect_contains("receive failure message", run_failure->error_message, "consumer failed with exit code 9");
    }
    test_helpers::expect_eq("receive failure checkpoint count", std::to_string(checkpoints.checkpoints.size()), "0");
    const auto* failed = find_event<btrfsbackup::backup::ActionFailed>(events.events);
    test_helpers::expect_true("receive failed action event", failed != nullptr, "missing failed action event");
    if (failed != nullptr) {
        test_helpers::expect_eq("receive failed error code", btrfsbackup::error_code_name(*failed->error_code), "transfer.consumer_failed");
        test_helpers::expect_contains("receive failed message", failed->message, "consumer failed with exit code 9");
    }
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

    const auto result = executor.execute(plan, events, cancellation);
    const auto* run_failure = failed_run(result);
    test_helpers::expect_true("commit failure result", run_failure != nullptr, "run should return a typed failure");
    if (run_failure != nullptr) {
        test_helpers::expect_contains("commit failure message", run_failure->error_message, "injected action failure");
    }
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

    const auto result = executor.execute(plan, events, cancellation);
    const auto* run_failure = failed_run(result);
    test_helpers::expect_true("commit cleanup failure type", run_failure != nullptr, "recovery requirement should fail the run");
    if (run_failure != nullptr) {
        test_helpers::expect_eq("commit cleanup result code", btrfsbackup::error_code_name(run_failure->error_code), "repository.recovery_required");
        test_helpers::expect_contains("commit cleanup failure message", run_failure->error_message, "repository requires recovery");
    }
    const auto* failed = find_event<btrfsbackup::backup::ActionFailed>(events.events);
    test_helpers::expect_true("commit cleanup failed event", failed != nullptr, "missing failed action event");
    if (failed != nullptr) {
        test_helpers::expect_eq("commit cleanup error code", btrfsbackup::error_code_name(*failed->error_code), "repository.recovery_required");
    }
}

void test_hook_timeout_emits_stable_error_code() {
    RecordingActionHandler handler;
    handler.should_throw = true;
    handler.throw_on = btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook;
    handler.coded_error_code = btrfsbackup::ErrorCode::HookBeforeSnapshotTimeout;
    RecordingTransferPipeline transfers;
    RecordingCheckpoints checkpoints;
    RecordingEvents events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers);
    btrfsbackup::backup::BackupRunExecutor executor(handler, async_transfers, checkpoints, safe_directories);

    btrfsbackup::backup::BackupRunPlan plan = plan_with_actions({
        action(btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook),
    });

    const auto result = executor.execute(plan, events, cancellation);
    const auto* run_failure = failed_run(result);
    test_helpers::expect_true("hook timeout type", run_failure != nullptr, "hook timeout should fail the run");
    if (run_failure != nullptr) {
        test_helpers::expect_eq("hook timeout result code", btrfsbackup::error_code_name(run_failure->error_code), "hook.before_snapshot_timeout");
        test_helpers::expect_contains("hook timeout message", run_failure->error_message, "coded action failure");
    }
    const auto* failed = find_event<btrfsbackup::backup::ActionFailed>(events.events);
    test_helpers::expect_true("hook timeout failed event", failed != nullptr, "missing failed action event");
    if (failed != nullptr) {
        test_helpers::expect_eq("hook timeout event code", btrfsbackup::error_code_name(*failed->error_code), "hook.before_snapshot_timeout");
    }
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
    test_helpers::expect_true("hook cancellation result", std::holds_alternative<btrfsbackup::backup::BackupRunExecutionCancelled>(result), "run should be cancelled");
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

    const auto result = executor.execute(plan, events, cancellation);
    test_helpers::expect_true("remote retention failure result", failed_run(result) != nullptr, "run should return a typed failure");
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

    const auto result = executor.execute(plan, events, cancellation);
    test_helpers::expect_true("local retention failure result", failed_run(result) != nullptr, "run should return a typed failure");
    test_helpers::expect_eq("local retention checkpoint count", std::to_string(checkpoints.checkpoints.size()), "5");
    test_helpers::expect_eq("local retention last checkpoint", action_name(checkpoints.checkpoints.back().action_kind), action_name(btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention));
    test_helpers::expect_true("local retention failed event", events.has_event(btrfsbackup::backup::BackupRunEventKind::ActionFailed), "missing failed action event");
}

} // namespace

int main() {
    test_lifecycle_events_do_not_have_synthetic_actions();
    test_every_action_uses_uniform_execution_semantics();
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
