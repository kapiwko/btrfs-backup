#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <state/config_fingerprint.hpp>
#include <state/run_state.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("run-state", name);
}

std::string read_file(const fs::path& path) {
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
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

    fs::remove_all(root);
}

void test_success_state_write_and_match() {
    fs::path root = test_root("success-state");
    fs::path state_dir = root / "state" / "profiles" / "default";

    btrfsbackup::write_success_state(state_dir, {
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

    fs::remove_all(root);
}

void test_pending_marker_write_read_and_clear() {
    fs::path root = test_root("pending-marker");
    fs::path state_dir = root / "state" / "profiles" / "default";
    fs::path snapshot = root / "local" / "root" / "root-2026-08-23T082504";
    fs::path final_snapshot = root / "remote" / "root" / snapshot.filename();

    btrfsbackup::write_pending_marker(state_dir, {
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
        btrfsbackup::read_pending_marker_field(marker, "local_snapshot_path"),
        snapshot.string()
    );

    btrfsbackup::clear_pending_marker(marker, state_dir);
    test_helpers::expect_eq("pending marker cleared", fs::exists(marker) ? "yes" : "no", "no");
    fs::remove_all(root);
}

void test_cancel_request_write_check_and_clear() {
    fs::path root = test_root("cancel-request");
    fs::path state_dir = root / "state" / "profiles" / "default";

    test_helpers::expect_true(
        "cancel initially absent",
        !btrfsbackup::cancel_requested(state_dir),
        "cancel request should not exist"
    );

    btrfsbackup::write_cancel_request(state_dir);
    test_helpers::expect_true("cancel requested", btrfsbackup::cancel_requested(state_dir), "cancel request missing");
    test_helpers::expect_eq(
        "cancel path",
        btrfsbackup::cancel_request_path(state_dir).string(),
        (state_dir / "cancel-request").string()
    );

    btrfsbackup::clear_cancel_request(state_dir);
    test_helpers::expect_true(
        "cancel cleared",
        !btrfsbackup::cancel_requested(state_dir),
        "cancel request should be cleared"
    );
    fs::remove_all(root);
}

} // namespace

int main() {
    test_config_fingerprint_matches_legacy_stream();
    test_success_state_write_and_match();
    test_pending_marker_write_read_and_clear();
    test_cancel_request_write_check_and_clear();

    return test_helpers::finish("run state tests");
}
