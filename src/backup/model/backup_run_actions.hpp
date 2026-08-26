// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <utility>
#include <variant>

#include <backup/model/backup_run_action.hpp>
#include <backup/pending_recovery_plan.hpp>
#include <backup/model/retention_plan.hpp>
#include <core/identifiers.hpp>
#include <config/model/profile.hpp>

namespace btrfsbackup {

struct RecoverPendingAction {
    SourceId source_id;
    PendingRecoveryPlan recovery;

    RecoverPendingAction(SourceId source_id, PendingRecoveryPlan recovery)
        : source_id(std::move(source_id)), recovery(std::move(recovery)) {
    }
};

struct CleanupIncomingAction {
    SourceId source_id;
    std::filesystem::path incoming_directory;

    CleanupIncomingAction(SourceId source_id, std::filesystem::path incoming_directory)
        : source_id(std::move(source_id)), incoming_directory(std::move(incoming_directory)) {
    }
};

enum class HookPhase { BeforeSnapshot,
                       AfterSnapshot };

struct RunHookAction {
    SourceId source_id;
    HookPhase phase;
    ProfileHookCommand hook;

    RunHookAction(SourceId source_id, HookPhase phase, ProfileHookCommand hook)
        : source_id(std::move(source_id)), phase(phase), hook(std::move(hook)) {
    }
};

struct CreateSnapshotAction {
    SourceId source_id;
    std::filesystem::path source;
    std::filesystem::path snapshot_directory;
    std::filesystem::path snapshot;
    std::filesystem::path final_remote_snapshot;
    std::filesystem::path profile_state_directory;
    RunId run_id;

    CreateSnapshotAction(
        SourceId source_id,
        std::filesystem::path source,
        std::filesystem::path snapshot_directory,
        std::filesystem::path snapshot,
        std::filesystem::path final_remote_snapshot,
        std::filesystem::path profile_state_directory,
        RunId run_id
    ) : source_id(std::move(source_id)),
        source(std::move(source)),
        snapshot_directory(std::move(snapshot_directory)),
        snapshot(std::move(snapshot)),
        final_remote_snapshot(std::move(final_remote_snapshot)),
        profile_state_directory(std::move(profile_state_directory)),
        run_id(std::move(run_id)) {
    }
};

struct SelectParentAction {
    SourceId source_id;
    std::optional<std::filesystem::path> parent;

    SelectParentAction(SourceId source_id, std::optional<std::filesystem::path> parent)
        : source_id(std::move(source_id)), parent(std::move(parent)) {
    }
};

struct SendReceiveAction {
    SourceId source_id;
    std::filesystem::path snapshot;
    std::optional<std::filesystem::path> parent;
    std::filesystem::path remote_snapshot_directory;
    std::filesystem::path incoming_run_directory;

    SendReceiveAction(
        SourceId source_id,
        std::filesystem::path snapshot,
        std::optional<std::filesystem::path> parent,
        std::filesystem::path remote_snapshot_directory,
        std::filesystem::path incoming_run_directory
    ) : source_id(std::move(source_id)),
        snapshot(std::move(snapshot)),
        parent(std::move(parent)),
        remote_snapshot_directory(std::move(remote_snapshot_directory)),
        incoming_run_directory(std::move(incoming_run_directory)) {
    }
};

struct VerifyReceivedAction {
    SourceId source_id;
    std::filesystem::path local_snapshot;
    std::filesystem::path received_snapshot;

    VerifyReceivedAction(
        SourceId source_id,
        std::filesystem::path local_snapshot,
        std::filesystem::path received_snapshot
    ) : source_id(std::move(source_id)),
        local_snapshot(std::move(local_snapshot)),
        received_snapshot(std::move(received_snapshot)) {
    }
};

struct CommitReceivedAction {
    SourceId source_id;
    std::filesystem::path local_snapshot;
    std::filesystem::path received_snapshot;
    std::filesystem::path final_snapshot;

    CommitReceivedAction(
        SourceId source_id,
        std::filesystem::path local_snapshot,
        std::filesystem::path received_snapshot,
        std::filesystem::path final_snapshot
    ) : source_id(std::move(source_id)),
        local_snapshot(std::move(local_snapshot)),
        received_snapshot(std::move(received_snapshot)),
        final_snapshot(std::move(final_snapshot)) {
    }
};

struct ApplyRemoteRetentionAction {
    SourceId source_id;
    RetentionPlan plan;

    ApplyRemoteRetentionAction(SourceId source_id, RetentionPlan plan)
        : source_id(std::move(source_id)), plan(std::move(plan)) {
    }
};

struct ApplyLocalRetentionAction {
    SourceId source_id;
    RetentionPlan plan;

    ApplyLocalRetentionAction(SourceId source_id, RetentionPlan plan)
        : source_id(std::move(source_id)), plan(std::move(plan)) {
    }
};

struct CleanupSourceAction {
    SourceId source_id;
    std::filesystem::path received_snapshot;
    std::filesystem::path incoming_run_directory;
    std::filesystem::path pending_marker;
    std::filesystem::path profile_state_directory;

    CleanupSourceAction(
        SourceId source_id,
        std::filesystem::path received_snapshot,
        std::filesystem::path incoming_run_directory,
        std::filesystem::path pending_marker,
        std::filesystem::path profile_state_directory
    ) : source_id(std::move(source_id)),
        received_snapshot(std::move(received_snapshot)),
        incoming_run_directory(std::move(incoming_run_directory)),
        pending_marker(std::move(pending_marker)),
        profile_state_directory(std::move(profile_state_directory)) {
    }
};

using BackupRunAction = std::variant<
    RecoverPendingAction,
    CleanupIncomingAction,
    RunHookAction,
    CreateSnapshotAction,
    SelectParentAction,
    SendReceiveAction,
    VerifyReceivedAction,
    CommitReceivedAction,
    ApplyRemoteRetentionAction,
    ApplyLocalRetentionAction,
    CleanupSourceAction>;

[[nodiscard]] BackupRunActionKind backup_run_action_kind(const BackupRunAction& action);
[[nodiscard]] const SourceId& backup_run_action_source_id(const BackupRunAction& action);

} // namespace btrfsbackup
