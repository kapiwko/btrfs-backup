// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <string>

#include <state/run_state.hpp>
#include <platform/linux/posix_durable_file_operations.hpp>
#include <core/file_permissions.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::platform::linux::PosixDurableFileOperations& durable_files() {
    static btrfsbackup::platform::linux::PosixDurableFileOperations files;
    return files;
}

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("run-state", name);
}

std::string read_file(const fs::path& path) {
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void test_success_state_write_and_match() {
    fs::path root = test_root("success-state");
    fs::path state_dir = root / "state" / "profiles" / "default";

    btrfsbackup::state::write_success_state(durable_files(), state_dir, {
                                                                            .date = "2026-08-23",
                                                                            .timestamp = "2026-08-23T08:25:04+02:00",
                                                                            .run_id = "20260823T062504Z-123-456",
                                                                            .profile_id = "default",
                                                                            .profile_name = "Default backup",
                                                                            .source_count = 2,
                                                                            .target_luks_uuid = "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE",
                                                                            .config_fingerprint = "630b159cf7939e5baf76ce27d4505a5cf68fe2995d5071c1f22e011e143b7c67",
                                                                        });

    fs::path state_file = state_dir / "last-success";
    test_helpers::expect_eq("success state exists", fs::is_regular_file(state_file) ? "yes" : "no", "yes");
    std::string content = read_file(state_file);
    test_helpers::expect_contains("success state date", content, "date=2026-08-23\n");
    test_helpers::expect_contains("success state source count", content, "source_count=2\n");
    test_helpers::expect_true(
        "success state match",
        btrfsbackup::state::last_success_matches(
            state_dir,
            "2026-08-23",
            "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
            "630b159cf7939e5baf76ce27d4505a5cf68fe2995d5071c1f22e011e143b7c67"
        ),
        "expected last success to match"
    );
    test_helpers::expect_true(
        "success state mismatch",
        !btrfsbackup::state::last_success_matches(
            state_dir,
            "2026-08-24",
            "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
            "630b159cf7939e5baf76ce27d4505a5cf68fe2995d5071c1f22e011e143b7c67"
        ),
        "stale date matched"
    );

    fs::remove_all(root);
}

void test_pending_marker_write_read_and_clear() {
    fs::path root = test_root("pending-marker");
    fs::path state_dir = root / "state" / "profiles" / "default";
    fs::path snapshot = root / "local" / "root" / "root-2026-08-23T082504";
    fs::path final_snapshot = root / "remote" / "root" / snapshot.filename();

    btrfsbackup::state::write_pending_marker(durable_files(), state_dir, {
                                                                             .source_name = "root",
                                                                             .local_snapshot_path = snapshot.string(),
                                                                             .final_snapshot_path = final_snapshot.string(),
                                                                             .run_id = "20260823T062504Z-123-456",
                                                                             .timestamp = "2026-08-23T08:25:04+02:00",
                                                                         });

    fs::path marker = state_dir / "pending-root";
    test_helpers::expect_eq("pending marker exists", fs::is_regular_file(marker) ? "yes" : "no", "yes");
    std::string content = read_file(marker);
    test_helpers::expect_contains("pending source", content, "source_name=root\n");
    test_helpers::expect_contains("pending final path", content, "final_snapshot_path=" + final_snapshot.string() + "\n");
    test_helpers::expect_contains("pending run", content, "run_id=20260823T062504Z-123-456\n");

    test_helpers::expect_eq(
        "pending path",
        btrfsbackup::state::read_pending_marker_field(marker, "local_snapshot_path"),
        snapshot.string()
    );

    btrfsbackup::state::clear_pending_marker(durable_files(), marker);
    test_helpers::expect_eq("pending marker cleared", fs::exists(marker) ? "yes" : "no", "no");
    fs::remove_all(root);
}

void test_cancel_request_write_check_and_clear() {
    fs::path root = test_root("cancel-request");
    fs::path state_dir = root / "state" / "profiles" / "default";
    const btrfsbackup::RunId run_id{"run-1"};

    test_helpers::expect_true(
        "cancel initially absent",
        !btrfsbackup::state::cancel_requested(state_dir),
        "cancel request should not exist"
    );

    btrfsbackup::state::write_active_run(durable_files(), state_dir, run_id);
    test_helpers::expect_true(
        "active run recorded",
        btrfsbackup::state::active_run(state_dir) == run_id,
        "active run identity missing"
    );

    btrfsbackup::state::write_cancel_request(durable_files(), state_dir, run_id);
    test_helpers::expect_true("cancel requested", btrfsbackup::state::cancel_requested(state_dir), "cancel request missing");
    test_helpers::expect_true(
        "cancel matches run",
        btrfsbackup::state::cancel_requested(state_dir, run_id),
        "cancel request run identity missing"
    );
    test_helpers::expect_eq(
        "cancel path",
        btrfsbackup::state::cancel_request_path(state_dir).string(),
        (state_dir / "cancel-request").string()
    );

    btrfsbackup::state::clear_cancel_request(durable_files(), state_dir, run_id);
    test_helpers::expect_true(
        "cancel cleared",
        !btrfsbackup::state::cancel_requested(state_dir),
        "cancel request should be cleared"
    );
    btrfsbackup::state::clear_active_run(durable_files(), state_dir, run_id);
    test_helpers::expect_true(
        "active run cleared",
        !btrfsbackup::state::active_run(state_dir).has_value(),
        "active run identity should be cleared"
    );
    fs::remove_all(root);
}

void test_legacy_cancel_marker_does_not_match_a_run() {
    fs::path root = test_root("legacy-cancel-request");
    fs::path state_dir = root / "state" / "profiles" / "default";
    durable_files().ensure_directory(state_dir, btrfsbackup::private_directory_permissions);
    durable_files().write_atomically(
        btrfsbackup::state::cancel_request_path(state_dir),
        "requested=1\n",
        btrfsbackup::private_file_permissions
    );

    test_helpers::expect_true(
        "legacy marker ignored",
        !btrfsbackup::state::cancel_requested(state_dir, btrfsbackup::RunId{"new-run"}),
        "legacy marker matched a new run"
    );
    fs::remove_all(root);
}

void test_mismatched_cancel_marker_is_not_consumed_or_cleared() {
    fs::path root = test_root("mismatched-cancel-request");
    fs::path state_dir = root / "state" / "profiles" / "default";
    const btrfsbackup::RunId active_run{"active-run"};
    const btrfsbackup::RunId other_run{"other-run"};
    btrfsbackup::state::write_cancel_request(durable_files(), state_dir, other_run);

    test_helpers::expect_true(
        "mismatched marker ignored",
        !btrfsbackup::state::cancel_requested(state_dir, active_run),
        "mismatched marker was consumed"
    );
    btrfsbackup::state::clear_cancel_request(durable_files(), state_dir, active_run);
    test_helpers::expect_true(
        "mismatched marker preserved",
        btrfsbackup::state::cancel_requested(state_dir, other_run),
        "one run cleared another run's marker"
    );
    fs::remove_all(root);
}

} // namespace

int main() {
    test_success_state_write_and_match();
    test_pending_marker_write_read_and_clear();
    test_cancel_request_write_check_and_clear();
    test_legacy_cancel_marker_does_not_match_a_run();
    test_mismatched_cancel_marker_is_not_consumed_or_cleared();

    return test_helpers::finish("run state tests");
}
