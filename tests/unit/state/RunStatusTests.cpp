// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/model/RunStatus.hpp>

#include <functional>
#include <type_traits>

#include <core/Errors.hpp>
#include <core/RuntimeTime.hpp>
#include "support/TestHelpers.hpp"

static_assert(!std::is_assignable_v<btrfsbackup::ProfileId&, btrfsbackup::RunId>);
static_assert(!std::is_assignable_v<btrfsbackup::ProfileId&, btrfsbackup::SourceId>);
static_assert(!std::is_assignable_v<btrfsbackup::RunId&, btrfsbackup::SourceId>);

namespace {

btrfsbackup::state::RunStatus running_status() {
    const auto time = *btrfsbackup::parse_utc_timestamp("2026-08-30T12:00:00Z");
    return {
        .profile_id = btrfsbackup::ProfileId{"default"},
        .profile_name = "Default",
        .run_id = btrfsbackup::RunId{"run-1"},
        .started_at = time,
        .updated_at = time,
        .can_cancel = true,
    };
}

void expect_invalid_status(
    const std::string& name,
    btrfsbackup::state::RunStatus status,
    const std::string& message
) {
    try {
        btrfsbackup::state::validate_run_status(status);
        test_helpers::expect_true(name, false, "status was accepted");
    } catch (const btrfsbackup::ValidationError& error) {
        test_helpers::expect_contains(name, error.what(), message);
    }
}

void test_run_state_names_are_stable() {
    test_helpers::expect_eq("running state", btrfsbackup::state::run_state_name(btrfsbackup::state::RunState::Running), "running");
    test_helpers::expect_eq("validating state", btrfsbackup::state::run_state_name(btrfsbackup::state::RunState::Validating), "validating");
    test_helpers::expect_eq("validated state", btrfsbackup::state::run_state_name(btrfsbackup::state::RunState::Validated), "validated");
    test_helpers::expect_eq("succeeded state", btrfsbackup::state::run_state_name(btrfsbackup::state::RunState::Succeeded), "succeeded");
    test_helpers::expect_eq("failed state", btrfsbackup::state::run_state_name(btrfsbackup::state::RunState::Failed), "failed");
    test_helpers::expect_eq("cancelled state", btrfsbackup::state::run_state_name(btrfsbackup::state::RunState::Cancelled), "cancelled");
    test_helpers::expect_eq("skipped state", btrfsbackup::state::run_state_name(btrfsbackup::state::RunState::Skipped), "skipped");
}

void test_run_phase_names_are_stable() {
    test_helpers::expect_eq("run phase", btrfsbackup::state::run_phase_name(btrfsbackup::state::RunPhase::RunStarted), "run-started");
    test_helpers::expect_eq("transfer phase", btrfsbackup::state::run_phase_name(btrfsbackup::state::RunPhase::Transferring), "transferring");
    test_helpers::expect_eq("recovery phase", btrfsbackup::state::run_phase_name(btrfsbackup::state::RunPhase::RecoverPending), "recover-pending");
    test_helpers::expect_eq("success phase", btrfsbackup::state::run_phase_name(btrfsbackup::state::RunPhase::Succeeded), "succeeded");
    test_helpers::expect_eq("validation phase", btrfsbackup::state::run_phase_name(btrfsbackup::state::RunPhase::ValidatingTarget), "validating-target");
    test_helpers::expect_eq("validated phase", btrfsbackup::state::run_phase_name(btrfsbackup::state::RunPhase::Validated), "validated");
}

void test_progress_accuracy_names_are_stable() {
    test_helpers::expect_eq(
        "indeterminate accuracy",
        btrfsbackup::state::progress_accuracy_name(btrfsbackup::state::ProgressAccuracy::Indeterminate),
        "indeterminate"
    );
    test_helpers::expect_eq(
        "estimated accuracy",
        btrfsbackup::state::progress_accuracy_name(btrfsbackup::state::ProgressAccuracy::Estimated),
        "estimated"
    );
    test_helpers::expect_eq(
        "exact accuracy",
        btrfsbackup::state::progress_accuracy_name(btrfsbackup::state::ProgressAccuracy::Exact),
        "exact"
    );
}

void test_run_status_rejects_inconsistent_terminal_state() {
    btrfsbackup::state::RunStatus status = running_status();
    status.state = btrfsbackup::state::RunState::Succeeded;
    expect_invalid_status("terminal timestamp", status, "requires finishedAt");

    status = running_status();
    status.finished_at = status.updated_at;
    expect_invalid_status("active timestamp", status, "must not contain finishedAt");

    status = running_status();
    status.state = btrfsbackup::state::RunState::Failed;
    status.phase = btrfsbackup::state::RunPhase::Failed;
    status.finished_at = status.updated_at;
    expect_invalid_status("failed error", status, "requires an error");

    status = running_status();
    status.state = btrfsbackup::state::RunState::Cancelled;
    status.phase = btrfsbackup::state::RunPhase::Cancelled;
    status.finished_at = status.updated_at;
    status.can_cancel = false;
    expect_invalid_status("cancelled error", status, "requires an error");

    status = running_status();
    status.state = btrfsbackup::state::RunState::Succeeded;
    status.phase = btrfsbackup::state::RunPhase::Succeeded;
    status.finished_at = status.updated_at;
    status.can_cancel = false;
    status.error = btrfsbackup::state::RunError{
        .code = btrfsbackup::ErrorCode::BackupFailed,
        .message = "stale failure",
    };
    expect_invalid_status("successful error", status, "must not contain an error");

    status = running_status();
    status.state = btrfsbackup::state::RunState::Succeeded;
    status.finished_at = status.updated_at;
    status.phase = btrfsbackup::state::RunPhase::Succeeded;
    expect_invalid_status("terminal cancellation", status, "cannot be cancelled");

    status = running_status();
    status.state = btrfsbackup::state::RunState::Succeeded;
    status.finished_at = status.updated_at;
    status.phase = btrfsbackup::state::RunPhase::Failed;
    status.can_cancel = false;
    expect_invalid_status("terminal phase", status, "state and phase do not match");

    status = running_status();
    status.phase = btrfsbackup::state::RunPhase::Succeeded;
    expect_invalid_status("running phase", status, "must not use a terminal phase");
}

void test_run_status_accepts_consistent_active_and_failed_states() {
    btrfsbackup::state::RunStatus active = running_status();
    btrfsbackup::state::validate_run_status(active);

    btrfsbackup::state::RunStatus failed = running_status();
    failed.state = btrfsbackup::state::RunState::Failed;
    failed.phase = btrfsbackup::state::RunPhase::Failed;
    failed.finished_at = failed.updated_at;
    failed.error = btrfsbackup::state::RunError{
        .code = btrfsbackup::ErrorCode::BackupFailed,
        .message = "failed",
    };
    failed.can_cancel = false;
    btrfsbackup::state::validate_run_status(failed);

    btrfsbackup::state::RunStatus cancelled = running_status();
    cancelled.state = btrfsbackup::state::RunState::Cancelled;
    cancelled.phase = btrfsbackup::state::RunPhase::Cancelled;
    cancelled.finished_at = cancelled.updated_at;
    cancelled.error = btrfsbackup::state::RunError{
        .code = btrfsbackup::ErrorCode::RunnerCancelled,
        .message = "cancelled",
    };
    cancelled.can_cancel = false;
    btrfsbackup::state::validate_run_status(cancelled);

    for (const auto [state, phase] : {
             std::pair{btrfsbackup::state::RunState::Validated, btrfsbackup::state::RunPhase::Validated},
             std::pair{btrfsbackup::state::RunState::Succeeded, btrfsbackup::state::RunPhase::Succeeded},
             std::pair{btrfsbackup::state::RunState::Skipped, btrfsbackup::state::RunPhase::Skipped},
         }) {
        btrfsbackup::state::RunStatus terminal = running_status();
        terminal.state = state;
        terminal.phase = phase;
        terminal.finished_at = terminal.updated_at;
        terminal.can_cancel = false;
        btrfsbackup::state::validate_run_status(terminal);
    }

    btrfsbackup::state::RunStatus validating = running_status();
    validating.state = btrfsbackup::state::RunState::Validating;
    validating.phase = btrfsbackup::state::RunPhase::ValidatingTarget;
    btrfsbackup::state::validate_run_status(validating);
}

} // namespace

int main() {
    test_run_state_names_are_stable();
    test_run_phase_names_are_stable();
    test_progress_accuracy_names_are_stable();
    test_run_status_rejects_inconsistent_terminal_state();
    test_run_status_accepts_consistent_active_and_failed_states();
    return test_helpers::finish("run status tests");
}
