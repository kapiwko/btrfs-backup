// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/runner_presenter.hpp>

#include <filesystem>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <backup/model/backup_run_actions.hpp>
#include <config/model/json.hpp>
#include <core/error_code.hpp>

namespace btrfsbackup::cli {
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

btrfsbackup::config::Json action_to_json(
    const btrfsbackup::backup::BackupRunAction& action,
    const fs::path& local_snapshot_dir,
    const fs::path& remote_snapshot_dir
) {
    const auto [primary_path, secondary_path] = std::visit([&](const auto& typed_action) {
        using Action = std::decay_t<decltype(typed_action)>;
        if constexpr (std::is_same_v<Action, btrfsbackup::backup::RecoverPendingAction>) {
            return std::pair{typed_action.recovery.local_snapshot_path, fs::path{}};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::CleanupIncomingAction>) {
            return std::pair{typed_action.incoming_directory, fs::path{}};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::CreateSnapshotAction>) {
            return std::pair{typed_action.snapshot, typed_action.source};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::SendReceiveAction>) {
            return std::pair{typed_action.snapshot, typed_action.incoming_run_directory};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::VerifyReceivedAction>) {
            return std::pair{typed_action.received_snapshot, typed_action.local_snapshot};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::CommitReceivedAction>) {
            return std::pair{typed_action.received_snapshot, typed_action.final_snapshot};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::ApplyRemoteRetentionAction>) {
            return std::pair{remote_snapshot_dir, fs::path{}};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::ApplyLocalRetentionAction>) {
            return std::pair{local_snapshot_dir, fs::path{}};
        } else {
            return std::pair{fs::path{}, fs::path{}};
        }
    },
                                                           action);
    btrfsbackup::config::Json result = {
        {"kind", action_name(btrfsbackup::backup::backup_run_action_kind(action))},
        {"sourceId", std::string(btrfsbackup::backup::backup_run_action_source_id(action).value())},
        {"primaryPath", primary_path.string()},
        {"secondaryPath", secondary_path.string()}
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

btrfsbackup::config::Json paths_to_json(const std::vector<btrfsbackup::backup::SnapshotInfo>& snapshots) {
    btrfsbackup::config::Json result = btrfsbackup::config::Json::array();
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

btrfsbackup::config::Json source_plan_to_json(const btrfsbackup::backup::BackupSourceRunPlan& source, bool include_actions) {
    const auto* transfer = find_action<btrfsbackup::backup::SendReceiveAction>(source);
    const auto* create_snapshot = find_action<btrfsbackup::backup::CreateSnapshotAction>(source);
    const auto* verify = find_action<btrfsbackup::backup::VerifyReceivedAction>(source);
    const auto* commit = find_action<btrfsbackup::backup::CommitReceivedAction>(source);
    const auto* recovery = find_action<btrfsbackup::backup::RecoverPendingAction>(source);
    const auto* local_retention = find_action<btrfsbackup::backup::ApplyLocalRetentionAction>(source);
    const auto* remote_retention = find_action<btrfsbackup::backup::ApplyRemoteRetentionAction>(source);
    btrfsbackup::config::Json result = {
        {"sourceId", std::string(source.source_id.value())},
        {"sourceSubvolume", create_snapshot != nullptr ? create_snapshot->source.string() : ""},
        {"localSnapshotPath", create_snapshot != nullptr ? create_snapshot->snapshot.string() : ""},
        {"remoteSnapshotDir", transfer != nullptr ? transfer->remote_snapshot_directory.string() : ""},
        {"incomingRunDir", transfer != nullptr ? transfer->incoming_run_directory.string() : ""},
        {"receivedSnapshotPath", verify != nullptr ? verify->received_snapshot.string() : ""},
        {"finalRemoteSnapshotPath", commit != nullptr ? commit->final_snapshot.string() : ""},
        {"incremental", transfer != nullptr && transfer->parent.has_value()},
        {"parentPath", transfer != nullptr && transfer->parent.has_value() ? btrfsbackup::config::Json(transfer->parent->string()) : btrfsbackup::config::Json(nullptr)},
        {"pendingRecoveryAction", recovery != nullptr ? "recover-pending" : "none"},
        {"localRetentionDelete", paths_to_json(retention_deletions(local_retention))},
        {"remoteRetentionDelete", paths_to_json(retention_deletions(remote_retention))}
    };
    if (include_actions) {
        const fs::path local_snapshot_dir = create_snapshot != nullptr ? create_snapshot->snapshot_directory : fs::path{};
        const fs::path remote_snapshot_dir = transfer != nullptr ? transfer->remote_snapshot_directory : fs::path{};
        btrfsbackup::config::Json actions = btrfsbackup::config::Json::array();
        for (const btrfsbackup::backup::BackupRunAction& action : source.actions()) {
            actions.push_back(action_to_json(action, local_snapshot_dir, remote_snapshot_dir));
        }
        result["actions"] = actions;
    }
    return result;
}

btrfsbackup::config::Json sources_to_json(
    const std::vector<btrfsbackup::backup::BackupSourceRunPlan>& sources,
    bool include_actions
) {
    btrfsbackup::config::Json result = btrfsbackup::config::Json::array();
    for (const btrfsbackup::backup::BackupSourceRunPlan& source : sources) {
        result.push_back(source_plan_to_json(source, include_actions));
    }
    return result;
}

int present_run_execution(
    const btrfsbackup::backup::BackupRunPlan& plan,
    bool skipped,
    bool cancelled,
    std::size_t actions_completed,
    std::ostream& output
) {
    const bool completed = !cancelled;
    output << btrfsbackup::config::Json{
                  {"schemaVersion", 1},
                  {"mode", "cpp-execute"},
                  {"profileId", std::string(plan.profile_id.value())},
                  {"runId", std::string(plan.run_id.value())},
                  {"completed", completed},
                  {"skipped", skipped},
                  {"cancelled", cancelled},
                  {"actionsCompleted", actions_completed},
                  {"sources", sources_to_json(plan.sources, false)}
              }.dump(2)
           << '\n';
    return completed ? 0 : 1;
}

} // namespace

void print_runner_usage(std::ostream& output) {
    output << "Usage: btrfs-backupctl runner COMMAND\n"
           << "\nCommands:\n"
           << "  plan --profile ID [--offline | --mount-target] [--timestamp TS] [--run-id ID] [--mountinfo PATH]\n"
           << "  execute --profile ID [--timestamp TS] [--run-id ID] [--force] [--validate]\n"
           << "  cancel --profile ID --run-id ID\n";
}

int present_runner_plan(const btrfsbackup::backup::BackupRunPlan& plan, std::ostream& output) {
    output << btrfsbackup::config::Json{
                  {"schemaVersion", 1},
                  {"mode", "shadow-plan"},
                  {"profileId", std::string(plan.profile_id.value())},
                  {"runId", std::string(plan.run_id.value())},
                  {"sources", sources_to_json(plan.sources, true)}
              }.dump(2)
           << '\n';
    return 0;
}

int present_runner_execution(const btrfsbackup::backup::BackupExecutionResult& result, std::ostream& output) {
    if (const auto* busy = std::get_if<btrfsbackup::backup::BackupExecutionBusy>(&result)) {
        output << btrfsbackup::config::Json{
                      {"schemaVersion", 1},
                      {"mode", "cpp-execute"},
                      {"profileId", std::string(busy->profile_id.value())},
                      {"runId", std::string(busy->run_id.value())},
                      {"completed", false},
                      {"skipped", false},
                      {"cancelled", false},
                      {"busy", true},
                      {"actionsCompleted", 0},
                      {"errorCode", error_code_name(busy->error_code)},
                      {"errorMessage", busy->error_message}
                  }.dump(2)
               << '\n';
        return 1;
    }
    if (const auto* validated = std::get_if<btrfsbackup::backup::BackupExecutionValidated>(&result)) {
        output << btrfsbackup::config::Json{
                      {"schemaVersion", 1},
                      {"mode", "cpp-validate"},
                      {"profileId", std::string(validated->plan.profile_id.value())},
                      {"runId", std::string(validated->plan.run_id.value())},
                      {"completed", true},
                      {"cancelled", false},
                      {"actionsCompleted", 0}
                  }
                      .dump(2)
               << '\n';
        return 0;
    }
    if (const auto* completed = std::get_if<btrfsbackup::backup::BackupExecutionCompleted>(&result)) {
        return present_run_execution(completed->plan, false, false, completed->actions_completed, output);
    }
    if (const auto* skipped = std::get_if<btrfsbackup::backup::BackupExecutionSkipped>(&result)) {
        return present_run_execution(skipped->plan, true, false, 0, output);
    }
    if (const auto* failed = std::get_if<btrfsbackup::backup::BackupExecutionFailed>(&result)) {
        output << btrfsbackup::config::Json{
                      {"schemaVersion", 1},
                      {"mode", "cpp-execute"},
                      {"profileId", std::string(failed->profile_id.value())},
                      {"runId", std::string(failed->run_id.value())},
                      {"completed", false},
                      {"skipped", false},
                      {"cancelled", false},
                      {"busy", false},
                      {"actionsCompleted", failed->actions_completed},
                      {"errorCode", error_code_name(failed->error_code)},
                      {"errorMessage", failed->error_message}
                  }.dump(2)
               << '\n';
        return 1;
    }
    const auto& cancelled = std::get<btrfsbackup::backup::BackupExecutionCancelled>(result);
    return present_run_execution(cancelled.plan, false, true, cancelled.actions_completed, output);
}

int present_runner_cancellation(const btrfsbackup::backup::CancelBackupResult& result, std::ostream& output) {
    return std::visit([&output](const auto& outcome) {
        using Outcome = std::decay_t<decltype(outcome)>;
        constexpr bool accepted = std::is_same_v<Outcome, btrfsbackup::backup::CancellationAccepted>;
        std::string error_code;
        if constexpr (std::is_same_v<Outcome, btrfsbackup::backup::CancellationStaleRun>) {
            error_code = error_code_name(btrfsbackup::ErrorCode::RunnerStaleRun);
        } else if constexpr (std::is_same_v<Outcome, btrfsbackup::backup::CancellationRunMismatch>) {
            error_code = error_code_name(btrfsbackup::ErrorCode::RunnerRunMismatch);
        }
        output << btrfsbackup::config::Json{
                      {"schemaVersion", 1},
                      {"mode", "cpp-cancel"},
                      {"profileId", std::string(outcome.profile_id.value())},
                      {"runId", std::string(outcome.run_id.value())},
                      {"cancelRequested", accepted},
                      {"errorCode", error_code}
                  }.dump(2)
               << '\n';
        return accepted ? 0 : 1;
    },
                      result);
}

} // namespace btrfsbackup::cli
