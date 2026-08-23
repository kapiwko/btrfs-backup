#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <btrfsbackup/command/config_fingerprint_command.hpp>
#include <btrfsbackup/command/run_state_command.hpp>
#include <btrfsbackup/command/status_write_command.hpp>
#include <btrfsbackup/config_fingerprint.hpp>
#include <btrfsbackup/run_state.hpp>
#include <btrfsbackup/status.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("run-state", name);
}

std::string read_file(const fs::path& path) {
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void test_write_status_command_writes_current_and_history() {
    fs::path root = test_root("write-status");

    btrfsbackup::command::write_status(
        root / "status",
        root / "history",
        {
            "--current",
            "--history",
            "--profile-id",
            "default",
            "--profile-name",
            "Default backup",
            "--run-id",
            "20260823T024407Z-4298-30158",
            "--state",
            "succeeded",
            "--phase",
            "succeeded",
            "--message",
            "Backup completed.",
            "--current-source-name",
            "Home",
            "--source-index",
            "1",
            "--source-count",
            "2",
            "--started-at",
            "2026-08-23T02:44:07+00:00",
            "--updated-at",
            "2026-08-23T02:45:07+00:00",
            "--finished-at",
            "2026-08-23T02:45:07+00:00",
            "--exit-code",
            "0",
        }
    );

    test_helpers::expect_eq("current exists", fs::is_regular_file(root / "status" / "default" / "current.json") ? "yes" : "no", "yes");
    test_helpers::expect_eq("history exists", fs::is_regular_file(root / "history" / "default" / "20260823T024407Z-4298-30158.json") ? "yes" : "no", "yes");
    test_helpers::expect_eq("last exists", fs::is_regular_file(root / "history" / "default" / "last.json") ? "yes" : "no", "yes");

    std::ostringstream output;
    btrfsbackup::command_status(root / "status", root / "history", {"--human"}, output);
    test_helpers::expect_contains("write human status", output.str(), "Default backup: succeeded\n");
    fs::remove_all(root);
}

void test_write_status_requires_target() {
    test_helpers::expect_validation_error(
        "write target",
        [&] {
            btrfsbackup::command::write_status(
                "/tmp/status",
                "/tmp/history",
                {
                    "--profile-id",
                    "default",
                    "--profile-name",
                    "Default backup",
                    "--run-id",
                    "20260823T024407Z-4298-30158",
                    "--state",
                    "running",
                    "--phase",
                    "starting",
                    "--started-at",
                    "2026-08-23T02:44:07+00:00",
                    "--updated-at",
                    "2026-08-23T02:44:07+00:00",
                }
            );
        },
        "requires --current or --history"
    );
}

void test_write_history_requires_finished_at() {
    test_helpers::expect_validation_error(
        "write history finished",
        [&] {
            btrfsbackup::command::write_status(
                "/tmp/status",
                "/tmp/history",
                {
                    "--history",
                    "--profile-id",
                    "default",
                    "--profile-name",
                    "Default backup",
                    "--run-id",
                    "20260823T024407Z-4298-30158",
                    "--state",
                    "succeeded",
                    "--phase",
                    "succeeded",
                    "--started-at",
                    "2026-08-23T02:44:07+00:00",
                    "--updated-at",
                    "2026-08-23T02:45:07+00:00",
                }
            );
        },
        "requires --finished-at"
    );
}

void test_config_fingerprint_matches_legacy_stream() {
    fs::path root = test_root("fingerprint");
    test_helpers::write_file(root / "main.env", "A=1\n");
    test_helpers::write_file(root / "10-root.conf", "SOURCE_NAME=root\n");
    test_helpers::write_file(root / "20-home.conf", "SOURCE_NAME=home\n");

    std::string digest = btrfsbackup::compute_config_fingerprint(
        "2.0.0",
        root / "main.env",
        {root / "10-root.conf", root / "20-home.conf"}
    );

    test_helpers::expect_eq("config fingerprint", digest, "f125982c7f64868550006c139bdba904248a93b4118afcc2332190e516494c34");

    std::ostringstream output;
    btrfsbackup::command::config_fingerprint(
        {
            "--version",
            "2.0.0",
            "--config",
            (root / "main.env").string(),
            "--source",
            (root / "10-root.conf").string(),
            "--source",
            (root / "20-home.conf").string(),
        },
        output
    );
    test_helpers::expect_eq("config fingerprint command", output.str(), digest + "\n");
    fs::remove_all(root);
}

void test_success_state_write_and_match() {
    fs::path root = test_root("success-state");
    fs::path state_dir = root / "state" / "profiles" / "default";

    btrfsbackup::command::write_success_state(
        {
            "--profile-state-dir",
            state_dir.string(),
            "--date",
            "2026-08-23",
            "--timestamp",
            "2026-08-23T08:25:04+02:00",
            "--run-id",
            "20260823T062504Z-123-456",
            "--profile-id",
            "default",
            "--profile-name",
            "Default backup",
            "--source-count",
            "2",
            "--target-luks-uuid",
            "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE",
            "--config-fingerprint",
            "630b159cf7939e5baf76ce27d4505a5cf68fe2995d5071c1f22e011e143b7c67",
        }
    );

    fs::path state_file = state_dir / "last-success";
    test_helpers::expect_eq("success state exists", fs::is_regular_file(state_file) ? "yes" : "no", "yes");
    std::string content = read_file(state_file);
    test_helpers::expect_contains("success state date", content, "date=2026-08-23\n");
    test_helpers::expect_contains("success state source count", content, "source_count=2\n");
    test_helpers::expect_true(
        "success state match",
        btrfsbackup::last_success_matches(
            state_dir,
            "2026-08-23",
            "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
            "630b159cf7939e5baf76ce27d4505a5cf68fe2995d5071c1f22e011e143b7c67"
        ),
        "expected last success to match"
    );
    test_helpers::expect_true(
        "success state mismatch",
        !btrfsbackup::last_success_matches(
            state_dir,
            "2026-08-24",
            "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
            "630b159cf7939e5baf76ce27d4505a5cf68fe2995d5071c1f22e011e143b7c67"
        ),
        "stale date matched"
    );

    std::ostringstream output;
    btrfsbackup::command::check_last_success(
        {
            "--profile-state-dir",
            state_dir.string(),
            "--today",
            "2026-08-23",
            "--target-luks-uuid",
            "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
            "--config-fingerprint",
            "630b159cf7939e5baf76ce27d4505a5cf68fe2995d5071c1f22e011e143b7c67",
        },
        output
    );
    test_helpers::expect_eq("success state command", output.str(), "yes\n");
    fs::remove_all(root);
}

void test_pending_marker_write_read_and_clear() {
    fs::path root = test_root("pending-marker");
    fs::path state_dir = root / "state" / "profiles" / "default";
    fs::path snapshot = root / "local" / "root" / "root-2026-08-23T082504";

    btrfsbackup::command::write_pending_marker(
        {
            "--profile-state-dir",
            state_dir.string(),
            "--source-name",
            "root",
            "--local-snapshot-path",
            snapshot.string(),
            "--run-id",
            "20260823T062504Z-123-456",
            "--timestamp",
            "2026-08-23T08:25:04+02:00",
        }
    );

    fs::path marker = state_dir / "pending-root";
    test_helpers::expect_eq("pending marker exists", fs::is_regular_file(marker) ? "yes" : "no", "yes");
    std::string content = read_file(marker);
    test_helpers::expect_contains("pending source", content, "source_name=root\n");
    test_helpers::expect_contains("pending run", content, "run_id=20260823T062504Z-123-456\n");

    std::ostringstream output;
    btrfsbackup::command::read_pending_marker(
        {
            "--marker",
            marker.string(),
            "--field",
            "local_snapshot_path",
        },
        output
    );
    test_helpers::expect_eq("pending path", output.str(), snapshot.string() + "\n");

    btrfsbackup::command::clear_pending_marker(
        {
            "--marker",
            marker.string(),
            "--profile-state-dir",
            state_dir.string(),
        }
    );
    test_helpers::expect_eq("pending marker cleared", fs::exists(marker) ? "yes" : "no", "no");
    fs::remove_all(root);
}

void test_migrate_legacy_state_moves_unclaimed_files() {
    fs::path root = test_root("legacy-state");
    fs::path state_dir = root / "state";
    fs::path profile_state_dir = state_dir / "profiles" / "default";
    test_helpers::write_file(state_dir / "last-success", "profile_id=legacy\n");
    test_helpers::write_file(state_dir / "pending-root", "local_snapshot_path=/snapshots/root-old\n");
    test_helpers::write_file(state_dir / "pending-home", "local_snapshot_path=/snapshots/home-old\n");
    test_helpers::write_file(profile_state_dir / "pending-home", "local_snapshot_path=/snapshots/home-current\n");

    btrfsbackup::command::migrate_legacy_state(
        {
            "--state-dir",
            state_dir.string(),
            "--profile-state-dir",
            profile_state_dir.string(),
        }
    );

    test_helpers::expect_eq("legacy success moved", fs::is_regular_file(profile_state_dir / "last-success") ? "yes" : "no", "yes");
    test_helpers::expect_eq("legacy success removed", fs::exists(state_dir / "last-success") ? "yes" : "no", "no");
    test_helpers::expect_eq("legacy pending moved", fs::is_regular_file(profile_state_dir / "pending-root") ? "yes" : "no", "yes");
    test_helpers::expect_eq("legacy pending removed", fs::exists(state_dir / "pending-root") ? "yes" : "no", "no");
    test_helpers::expect_eq("conflicting pending kept", fs::is_regular_file(state_dir / "pending-home") ? "yes" : "no", "yes");

    std::string content = read_file(profile_state_dir / "pending-home");
    test_helpers::expect_contains("current pending preserved", content, "/snapshots/home-current");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_write_status_command_writes_current_and_history();
    test_write_status_requires_target();
    test_write_history_requires_finished_at();
    test_config_fingerprint_matches_legacy_stream();
    test_success_state_write_and_match();
    test_pending_marker_write_read_and_clear();
    test_migrate_legacy_state_moves_unclaimed_files();

    return test_helpers::finish("run state tests");
}
