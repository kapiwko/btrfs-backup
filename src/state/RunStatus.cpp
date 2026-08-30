// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/RunStatus.hpp>

#include <core/Errors.hpp>

namespace btrfsbackup::state {

namespace {

bool terminal(RunState state) {
    return state != RunState::Running && state != RunState::Validating;
}

bool terminal_phase_matches(RunState state, RunPhase phase) {
    switch (state) {
    case RunState::Validated:
        return phase == RunPhase::Validated;
    case RunState::Succeeded:
        return phase == RunPhase::Succeeded;
    case RunState::Failed:
        return phase != RunPhase::Validated && phase != RunPhase::Succeeded && phase != RunPhase::Cancelled &&
            phase != RunPhase::Skipped;
    case RunState::Cancelled:
        return phase == RunPhase::Cancelled;
    case RunState::Skipped:
        return phase == RunPhase::Skipped;
    case RunState::Running:
    case RunState::Validating:
        return true;
    }
    return false;
}

bool is_terminal_phase(RunPhase phase) {
    return phase == RunPhase::Validated || phase == RunPhase::Succeeded || phase == RunPhase::Failed ||
        phase == RunPhase::Cancelled || phase == RunPhase::Skipped;
}

} // namespace

void validate_run_status(const RunStatus& status) {
    const bool is_terminal = terminal(status.state);
    const bool has_terminal_error = status.state == RunState::Failed || status.state == RunState::Cancelled;
    if (is_terminal != status.finished_at.has_value()) {
        throw ValidationError(is_terminal ? "terminal run status requires finishedAt" : "active run status must not contain finishedAt");
    }
    if (is_terminal && !terminal_phase_matches(status.state, status.phase)) {
        throw ValidationError("terminal run state and phase do not match");
    }
    if (status.state == RunState::Validating && status.phase != RunPhase::ValidatingTarget) {
        throw ValidationError("validating run status requires validatingTarget phase");
    }
    if (status.state == RunState::Running && is_terminal_phase(status.phase)) {
        throw ValidationError("running run status must not use a terminal phase");
    }
    if (has_terminal_error && !status.error.has_value()) {
        throw ValidationError("failed or cancelled run status requires an error");
    }
    if (is_terminal && !has_terminal_error && status.error.has_value()) {
        throw ValidationError("successful terminal run status must not contain an error");
    }
    if (is_terminal && status.can_cancel) {
        throw ValidationError("terminal run status cannot be cancelled");
    }
}

std::string run_state_name(RunState state) {
    switch (state) {
    case RunState::Running:
        return "running";
    case RunState::Validating:
        return "validating";
    case RunState::Validated:
        return "validated";
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
    case RunPhase::Validated:
        return "validated";
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
