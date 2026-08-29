// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/run_status.hpp>

#include <type_traits>

#include "support/test_helpers.hpp"

static_assert(!std::is_assignable_v<btrfsbackup::ProfileId&, btrfsbackup::RunId>);
static_assert(!std::is_assignable_v<btrfsbackup::ProfileId&, btrfsbackup::SourceId>);
static_assert(!std::is_assignable_v<btrfsbackup::RunId&, btrfsbackup::SourceId>);

namespace {

void test_run_state_names_are_stable() {
    test_helpers::expect_eq("running state", btrfsbackup::state::run_state_name(btrfsbackup::state::RunState::Running), "running");
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

} // namespace

int main() {
    test_run_state_names_are_stable();
    test_run_phase_names_are_stable();
    test_progress_accuracy_names_are_stable();
    return test_helpers::finish("run status tests");
}
