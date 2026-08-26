// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/model/backup_run_actions.hpp>

#include <type_traits>

namespace btrfsbackup {

BackupRunActionKind backup_run_action_kind(const BackupRunAction& action) {
    return std::visit([](const auto& typed_action) {
        using Action = std::decay_t<decltype(typed_action)>;
        if constexpr (std::is_same_v<Action, RecoverPendingAction>) {
            return BackupRunActionKind::RecoverPending;
        } else if constexpr (std::is_same_v<Action, CleanupIncomingAction>) {
            return BackupRunActionKind::CleanupIncoming;
        } else if constexpr (std::is_same_v<Action, RunHookAction>) {
            return typed_action.phase == HookPhase::BeforeSnapshot
                ? BackupRunActionKind::BeforeSnapshotHook
                : BackupRunActionKind::AfterSnapshotHook;
        } else if constexpr (std::is_same_v<Action, CreateSnapshotAction>) {
            return BackupRunActionKind::CreateSnapshot;
        } else if constexpr (std::is_same_v<Action, SelectParentAction>) {
            return BackupRunActionKind::SelectParent;
        } else if constexpr (std::is_same_v<Action, SendReceiveAction>) {
            return BackupRunActionKind::SendReceive;
        } else if constexpr (std::is_same_v<Action, VerifyReceivedAction>) {
            return BackupRunActionKind::VerifyReceived;
        } else if constexpr (std::is_same_v<Action, CommitReceivedAction>) {
            return BackupRunActionKind::CommitReceived;
        } else if constexpr (std::is_same_v<Action, ApplyRemoteRetentionAction>) {
            return BackupRunActionKind::ApplyRemoteRetention;
        } else if constexpr (std::is_same_v<Action, ApplyLocalRetentionAction>) {
            return BackupRunActionKind::ApplyLocalRetention;
        } else {
            return BackupRunActionKind::CleanupSource;
        }
    },
                      action);
}

const SourceId& backup_run_action_source_id(const BackupRunAction& action) {
    return std::visit([](const auto& typed_action) -> const SourceId& {
        return typed_action.source_id;
    },
                      action);
}

} // namespace btrfsbackup
