// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/run_status.hpp>

namespace btrfsbackup::state {

std::string run_state_name(RunState state) {
    switch (state) {
    case RunState::Running:
        return "running";
    case RunState::Succeeded:
        return "succeeded";
    case RunState::Failed:
        return "failed";
    case RunState::Cancelled:
        return "cancelled";
    case RunState::Skipped:
        return "skipped";
    }
    return "running";
}

std::string run_phase_name(RunPhase phase) {
    switch (phase) {
    case RunPhase::RunStarted:
        return "run-started";
    case RunPhase::SourceStarted:
        return "source-started";
    case RunPhase::RecoverPending:
        return "recover-pending";
    case RunPhase::CleanupIncoming:
        return "cleanup-incoming";
    case RunPhase::BeforeSnapshotHook:
        return "before-snapshot-hook";
    case RunPhase::CreateSnapshot:
        return "create-snapshot";
    case RunPhase::AfterSnapshotHook:
        return "after-snapshot-hook";
    case RunPhase::SendReceive:
        return "send-receive";
    case RunPhase::Sizing:
        return "sizing";
    case RunPhase::Transferring:
        return "transferring";
    case RunPhase::VerifyReceived:
        return "verify-received";
    case RunPhase::CommitReceived:
        return "commit-received";
    case RunPhase::ApplyRemoteRetention:
        return "apply-remote-retention";
    case RunPhase::ApplyLocalRetention:
        return "apply-local-retention";
    case RunPhase::CleanupSource:
        return "cleanup-source";
    case RunPhase::SourceCompleted:
        return "source-completed";
    case RunPhase::Succeeded:
        return "succeeded";
    case RunPhase::Failed:
        return "failed";
    case RunPhase::Cancelled:
        return "cancelled";
    case RunPhase::Skipped:
        return "skipped";
    case RunPhase::ValidatingTarget:
        return "validating-target";
    }
    return "run-started";
}

std::string progress_accuracy_name(ProgressAccuracy accuracy) {
    switch (accuracy) {
    case ProgressAccuracy::Indeterminate:
        return "indeterminate";
    case ProgressAccuracy::Estimated:
        return "estimated";
    case ProgressAccuracy::Exact:
        return "exact";
    }
    return "indeterminate";
}

} // namespace btrfsbackup::state
