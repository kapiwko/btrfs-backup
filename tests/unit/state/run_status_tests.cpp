#include <state/run_status.hpp>

#include "support/test_helpers.hpp"

namespace {

void test_run_state_names_are_stable() {
    test_helpers::expect_eq("running state", btrfsbackup::run_state_name(btrfsbackup::RunState::Running), "running");
    test_helpers::expect_eq("succeeded state", btrfsbackup::run_state_name(btrfsbackup::RunState::Succeeded), "succeeded");
    test_helpers::expect_eq("failed state", btrfsbackup::run_state_name(btrfsbackup::RunState::Failed), "failed");
    test_helpers::expect_eq("cancelled state", btrfsbackup::run_state_name(btrfsbackup::RunState::Cancelled), "cancelled");
    test_helpers::expect_eq("skipped state", btrfsbackup::run_state_name(btrfsbackup::RunState::Skipped), "skipped");
}

void test_run_phase_names_are_stable() {
    test_helpers::expect_eq("run phase", btrfsbackup::run_phase_name(btrfsbackup::RunPhase::RunStarted), "run-started");
    test_helpers::expect_eq("transfer phase", btrfsbackup::run_phase_name(btrfsbackup::RunPhase::Transferring), "transferring");
    test_helpers::expect_eq("recovery phase", btrfsbackup::run_phase_name(btrfsbackup::RunPhase::RecoverPending), "recover-pending");
    test_helpers::expect_eq("success phase", btrfsbackup::run_phase_name(btrfsbackup::RunPhase::Succeeded), "succeeded");
    test_helpers::expect_eq("validation phase", btrfsbackup::run_phase_name(btrfsbackup::RunPhase::ValidatingTarget), "validating-target");
}

void test_progress_accuracy_names_are_stable() {
    test_helpers::expect_eq(
        "indeterminate accuracy",
        btrfsbackup::progress_accuracy_name(btrfsbackup::ProgressAccuracy::Indeterminate),
        "indeterminate"
    );
    test_helpers::expect_eq(
        "estimated accuracy",
        btrfsbackup::progress_accuracy_name(btrfsbackup::ProgressAccuracy::Estimated),
        "estimated"
    );
}

} // namespace

int main() {
    test_run_state_names_are_stable();
    test_run_phase_names_are_stable();
    test_progress_accuracy_names_are_stable();
    return test_helpers::finish("run status tests");
}
