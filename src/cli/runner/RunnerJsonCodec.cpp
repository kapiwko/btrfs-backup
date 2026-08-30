// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/runner/RunnerJsonCodec.hpp>

#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <backup/model/BackupRunActions.hpp>
#include <config/json/Json.hpp>
#include <core/ErrorCode.hpp>

namespace btrfsbackup::cli::runner {
namespace {

namespace fs = std::filesystem;

std::string action_name(btrfsbackup::backup::BackupRunActionKind kind) {
    switch (kind) {
    case btrfsbackup::backup::BackupRunActionKind::RecoverPending:
        return "recover-pending";
    case btrfsbackup::backup::BackupRunActionKind::CleanupIncoming:
        return "cleanup-incoming";
    case btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook:
        return "before-snapshot-hook";
    case btrfsbackup::backup::BackupRunActionKind::CreateSnapshot:
        return "create-snapshot";
    case btrfsbackup::backup::BackupRunActionKind::AfterSnapshotHook:
        return "after-snapshot-hook";
    case btrfsbackup::backup::BackupRunActionKind::SendReceive:
        return "send-receive";
    case btrfsbackup::backup::BackupRunActionKind::VerifyReceived:
        return "verify-received";
    case btrfsbackup::backup::BackupRunActionKind::CommitReceived:
        return "commit-received";
    case btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention:
        return "apply-remote-retention";
    case btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention:
        return "apply-local-retention";
    case btrfsbackup::backup::BackupRunActionKind::CleanupSource:
        return "cleanup-source";
    }
    return "unknown";
}

struct ActionPathContext {
    const fs::path& local_snapshot_directory;
    const fs::path& remote_snapshot_directory;
};

struct ActionPaths {
    fs::path primary;
    fs::path secondary;
};

ActionPaths action_paths(const btrfsbackup::backup::RecoverPendingAction& action, const ActionPathContext&) {
    return {action.recovery.pending_snapshot_path, {}};
}

ActionPaths action_paths(const btrfsbackup::backup::CleanupIncomingAction& action, const ActionPathContext&) {
    return {action.incoming_directory, {}};
}

ActionPaths action_paths(const btrfsbackup::backup::CreateSnapshotAction& action, const ActionPathContext&) {
    return {action.snapshot, action.source};
}

ActionPaths action_paths(const btrfsbackup::backup::SendReceiveAction& action, const ActionPathContext&) {
    return {action.snapshot, action.incoming_run_directory};
}

ActionPaths action_paths(const btrfsbackup::backup::VerifyReceivedAction& action, const ActionPathContext&) {
    return {action.received_snapshot, action.local_snapshot};
}

ActionPaths action_paths(const btrfsbackup::backup::CommitReceivedAction& action, const ActionPathContext&) {
    return {action.received_snapshot, action.final_snapshot};
}

ActionPaths action_paths(const btrfsbackup::backup::ApplyRemoteRetentionAction&, const ActionPathContext& context) {
    return {context.remote_snapshot_directory, {}};
}

ActionPaths action_paths(const btrfsbackup::backup::ApplyLocalRetentionAction&, const ActionPathContext& context) {
    return {context.local_snapshot_directory, {}};
}

template <typename Action>
ActionPaths action_paths(const Action&, const ActionPathContext&) {
    return {};
}

btrfsbackup::config::json::Json action_to_json(
    const btrfsbackup::backup::BackupRunAction& action,
    const fs::path& local_snapshot_dir,
    const fs::path& remote_snapshot_dir
) {
    const ActionPathContext context{local_snapshot_dir, remote_snapshot_dir};
    const ActionPaths paths = std::visit([&](const auto& typed_action) {
        return action_paths(typed_action, context);
    },
                                         action);
    btrfsbackup::config::json::Json result = {
        {"kind", action_name(btrfsbackup::backup::backup_run_action_kind(action))},
        {"sourceId", std::string(btrfsbackup::backup::backup_run_action_source_id(action).value())},
        {"primaryPath", paths.primary.string()},
        {"secondaryPath", paths.secondary.string()}
    };
    if (const auto* hook_action = std::get_if<btrfsbackup::backup::RunHookAction>(&action)) {
        result["hook"] = {
            {"type", "program"},
            {"program", hook_action->hook.program.value().string()},
            {"arguments", hook_action->hook.arguments},
            {"timeoutSeconds", hook_action->hook.timeout.count()}
        };
    }
    return result;
}

btrfsbackup::config::json::Json paths_to_json(const std::vector<btrfsbackup::backup::SnapshotInfo>& snapshots) {
    btrfsbackup::config::json::Json result = btrfsbackup::config::json::Json::array();
    for (const btrfsbackup::backup::SnapshotInfo& snapshot : snapshots) {
        result.push_back(snapshot.path.string());
    }
    return result;
}

template <typename Action>
const Action* find_action(const btrfsbackup::backup::BackupSourceRunPlan& source) {
    for (const btrfsbackup::backup::BackupRunAction& action : source.actions()) {
        if (const auto* typed_action = std::get_if<Action>(&action)) {
            return typed_action;
        }
    }
    return nullptr;
}

template <typename RetentionAction>
const std::vector<btrfsbackup::backup::SnapshotInfo>& retention_deletions(const RetentionAction* action) {
    static const std::vector<btrfsbackup::backup::SnapshotInfo> empty;
    return action == nullptr ? empty : action->plan.delete_snapshots;
}

btrfsbackup::config::json::Json source_plan_to_json(const btrfsbackup::backup::BackupSourceRunPlan& source, bool include_actions) {
    const auto* transfer = find_action<btrfsbackup::backup::SendReceiveAction>(source);
    const auto* create_snapshot = find_action<btrfsbackup::backup::CreateSnapshotAction>(source);
    const auto* verify = find_action<btrfsbackup::backup::VerifyReceivedAction>(source);
    const auto* commit = find_action<btrfsbackup::backup::CommitReceivedAction>(source);
    const auto* recovery = find_action<btrfsbackup::backup::RecoverPendingAction>(source);
    const auto* local_retention = find_action<btrfsbackup::backup::ApplyLocalRetentionAction>(source);
    const auto* remote_retention = find_action<btrfsbackup::backup::ApplyRemoteRetentionAction>(source);
    btrfsbackup::config::json::Json result = {
        {"sourceId", std::string(source.source_id.value())},
        {"sourceSubvolume", create_snapshot != nullptr ? create_snapshot->source.string() : ""},
        {"localSnapshotPath", create_snapshot != nullptr ? create_snapshot->snapshot.string() : ""},
        {"remoteSnapshotDir", transfer != nullptr ? transfer->remote_snapshot_directory.string() : ""},
        {"incomingRunDir", transfer != nullptr ? transfer->incoming_run_directory.string() : ""},
        {"receivedSnapshotPath", verify != nullptr ? verify->received_snapshot.string() : ""},
        {"finalRemoteSnapshotPath", commit != nullptr ? commit->final_snapshot.string() : ""},
        {"incremental", transfer != nullptr && transfer->parent.has_value()},
        {"parentPath", transfer != nullptr && transfer->parent.has_value() ? btrfsbackup::config::json::Json(transfer->parent->string()) : btrfsbackup::config::json::Json(nullptr)},
        {"pendingRecoveryAction", recovery != nullptr ? "recover-pending" : "none"},
        {"localRetentionDelete", paths_to_json(retention_deletions(local_retention))},
        {"remoteRetentionDelete", paths_to_json(retention_deletions(remote_retention))}
    };
    if (include_actions) {
        const fs::path local_snapshot_dir = create_snapshot != nullptr ? create_snapshot->snapshot_directory : fs::path{};
        const fs::path remote_snapshot_dir = transfer != nullptr ? transfer->remote_snapshot_directory : fs::path{};
        btrfsbackup::config::json::Json actions = btrfsbackup::config::json::Json::array();
        for (const btrfsbackup::backup::BackupRunAction& action : source.actions()) {
            actions.push_back(action_to_json(action, local_snapshot_dir, remote_snapshot_dir));
        }
        result["actions"] = actions;
    }
    return result;
}

btrfsbackup::config::json::Json sources_to_json(
    const std::vector<btrfsbackup::backup::BackupSourceRunPlan>& sources,
    bool include_actions
) {
    btrfsbackup::config::json::Json result = btrfsbackup::config::json::Json::array();
    for (const btrfsbackup::backup::BackupSourceRunPlan& source : sources) {
        result.push_back(source_plan_to_json(source, include_actions));
    }
    return result;
}

std::string completion_warning_component_name(
    btrfsbackup::backup::BackupCompletionWarningComponent component
) {
    switch (component) {
    case btrfsbackup::backup::BackupCompletionWarningComponent::SuccessLedger:
        return "success-ledger";
    case btrfsbackup::backup::BackupCompletionWarningComponent::TerminalStatus:
        return "terminal-status";
    }
    return "unknown";
}

btrfsbackup::config::json::Json completion_warnings_to_json(
    const std::vector<btrfsbackup::backup::BackupCompletionWarning>& warnings
) {
    btrfsbackup::config::json::Json result = btrfsbackup::config::json::Json::array();
    for (const btrfsbackup::backup::BackupCompletionWarning& warning : warnings) {
        result.push_back({
            {"component", completion_warning_component_name(warning.component)},
            {"errorCode", error_code_name(warning.error_code)},
            {"message", warning.message},
        });
    }
    return result;
}

EncodedRunnerResponse encode_json(btrfsbackup::config::json::Json document, int exit_code) {
    return {.output = document.dump(2) + '\n', .exit_code = exit_code};
}

EncodedRunnerResponse encode_run_execution(
    const btrfsbackup::backup::BackupRunPlan& plan,
    bool skipped,
    bool cancelled,
    std::size_t actions_completed,
    const std::vector<btrfsbackup::backup::BackupCompletionWarning>& warnings
) {
    const bool completed = !cancelled;
    return encode_json({{"schemaVersion", 1}, {"mode", "cpp-execute"}, {"profileId", std::string(plan.profile_id.value())}, {"runId", std::string(plan.run_id.value())}, {"completed", completed}, {"skipped", skipped}, {"cancelled", cancelled}, {"degraded", !warnings.empty()}, {"actionsCompleted", actions_completed}, {"warnings", completion_warnings_to_json(warnings)}, {"sources", sources_to_json(plan.sources, false)}}, completed ? 0 : 1);
}

} // namespace

EncodedRunnerResponse encode_runner_plan(const btrfsbackup::backup::BackupRunPlan& plan) {
    return encode_json({{"schemaVersion", 1}, {"mode", "shadow-plan"}, {"profileId", std::string(plan.profile_id.value())}, {"runId", std::string(plan.run_id.value())}, {"sources", sources_to_json(plan.sources, true)}}, 0);
}

EncodedRunnerResponse encode_runner_execution(const btrfsbackup::backup::BackupExecutionResult& result) {
    if (const auto* busy = std::get_if<btrfsbackup::backup::BackupExecutionBusy>(&result)) {
        return encode_json({{"schemaVersion", 1}, {"mode", "cpp-execute"}, {"profileId", std::string(busy->profile_id.value())}, {"runId", std::string(busy->run_id.value())}, {"completed", false}, {"skipped", false}, {"cancelled", false}, {"busy", true}, {"actionsCompleted", 0}, {"errorCode", error_code_name(busy->error_code)}, {"errorMessage", busy->error_message}}, 1);
    }
    if (const auto* validated = std::get_if<btrfsbackup::backup::BackupExecutionValidated>(&result)) {
        return encode_json({{"schemaVersion", 1}, {"mode", "cpp-validate"}, {"profileId", std::string(validated->plan.profile_id.value())}, {"runId", std::string(validated->plan.run_id.value())}, {"completed", true}, {"cancelled", false}, {"actionsCompleted", 0}}, 0);
    }
    if (const auto* completed = std::get_if<btrfsbackup::backup::BackupExecutionCompleted>(&result)) {
        return encode_run_execution(completed->plan, false, false, completed->actions_completed, completed->warnings);
    }
    if (const auto* skipped = std::get_if<btrfsbackup::backup::BackupExecutionSkipped>(&result)) {
        return encode_run_execution(skipped->plan, true, false, 0, {});
    }
    if (const auto* failed = std::get_if<btrfsbackup::backup::BackupExecutionFailed>(&result)) {
        return encode_json({{"schemaVersion", 1}, {"mode", "cpp-execute"}, {"profileId", std::string(failed->profile_id.value())}, {"runId", std::string(failed->run_id.value())}, {"completed", false}, {"skipped", false}, {"cancelled", false}, {"busy", false}, {"actionsCompleted", failed->actions_completed}, {"errorCode", error_code_name(failed->error_code)}, {"errorMessage", failed->error_message}}, 1);
    }
    const auto& cancelled = std::get<btrfsbackup::backup::BackupExecutionCancelled>(result);
    return encode_run_execution(cancelled.plan, false, true, cancelled.actions_completed, {});
}

EncodedRunnerResponse encode_runner_cancellation(const btrfsbackup::backup::CancelBackupResult& result) {
    return std::visit([](const auto& outcome) {
        using Outcome = std::decay_t<decltype(outcome)>;
        constexpr bool accepted = std::is_same_v<Outcome, btrfsbackup::backup::CancellationAccepted>;
        std::string error_code;
        if constexpr (std::is_same_v<Outcome, btrfsbackup::backup::CancellationStaleRun>) {
            error_code = error_code_name(btrfsbackup::ErrorCode::RunnerStaleRun);
        } else if constexpr (std::is_same_v<Outcome, btrfsbackup::backup::CancellationRunMismatch>) {
            error_code = error_code_name(btrfsbackup::ErrorCode::RunnerRunMismatch);
        }
        return encode_json({{"schemaVersion", 1}, {"mode", "cpp-cancel"}, {"profileId", std::string(outcome.profile_id.value())}, {"runId", std::string(outcome.run_id.value())}, {"cancelRequested", accepted}, {"errorCode", error_code}}, accepted ? 0 : 1);
    },
                      result);
}

} // namespace btrfsbackup::cli::runner
