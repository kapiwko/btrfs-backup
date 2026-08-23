#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include <btrfsbackup/config_fingerprint.hpp>
#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/history.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/profile_list.hpp>
#include <btrfsbackup/run_state.hpp>
#include <btrfsbackup/run_state_command.hpp>
#include <btrfsbackup/source_definition.hpp>
#include <btrfsbackup/status.hpp>
#include <btrfsbackup/status_write_command.hpp>

namespace fs = std::filesystem;

namespace {

using btrfsbackup::ValidationError;

int failures = 0;

void fail(const std::string& name, const std::string& message) {
    ++failures;
    std::cerr << "not ok - " << name << ": " << message << '\n';
}

void expect_eq(const std::string& name, const std::string& actual, const std::string& expected) {
    if (actual != expected) {
        fail(name, "expected [" + expected + "], got [" + actual + "]");
    }
}

void expect_contains(const std::string& name, const std::string& actual, const std::string& needle) {
    if (actual.find(needle) == std::string::npos) {
        fail(name, "missing [" + needle + "] in [" + actual + "]");
    }
}

void expect_true(const std::string& name, bool condition, const std::string& message) {
    if (!condition) {
        fail(name, message);
    }
}

void expect_validation_error(const std::string& name, const std::function<void()>& fn, const std::string& expected) {
    try {
        fn();
        fail(name, "expected ValidationError");
    } catch (const ValidationError& exc) {
        std::string message = exc.what();
        if (message.find(expected) == std::string::npos) {
            fail(name, "unexpected error: " + message);
        }
    } catch (const std::exception& exc) {
        fail(name, std::string("unexpected exception: ") + exc.what());
    }
}

fs::path test_root(const std::string& name) {
    fs::path root = fs::temp_directory_path() / ("btrfsbackup-ctl-tests-" + std::to_string(getpid()) + "-" + name);
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << content;
}

void test_history_without_directory_returns_empty_array() {
    fs::path root = test_root("history-empty");
    std::ostringstream output;

    btrfsbackup::command_history(root / "history", {}, output);

    expect_eq("history without directory", output.str(), "[]\n");
    fs::remove_all(root);
}

void test_status_falls_back_to_last_json() {
    fs::path root = test_root("status-fallback");
    write_file(root / "history" / "default" / "last.json", "{\"profileId\":\"default\",\"state\":\"ok\"}\n");
    std::ostringstream output;

    btrfsbackup::command_status(root / "status", root / "history", {}, output);

    expect_eq("status fallback", output.str(), "{\"profileId\":\"default\",\"state\":\"ok\"}\n");
    fs::remove_all(root);
}

void test_list_profiles_from_json_and_env_files() {
    fs::path root = test_root("profiles");
    write_file(root / "profiles.d" / "alpha.env", "");
    write_file(root / "profiles.d" / "default.env", "");
    write_file(root / "profiles" / "beta" / "profile.json", "{}\n");
    write_file(root / "profiles" / "default" / "profile.json", "{}\n");
    std::ostringstream output;

    btrfsbackup::command_list_profiles(root / "profiles.d", root / "profiles", output);

    expect_eq("list profiles", output.str(), "alpha\nbeta\ndefault\n");
    fs::remove_all(root);
}

void test_status_human_format() {
    fs::path root = test_root("human");
    write_file(
        root / "status" / "default" / "current.json",
        "{"
        "\"profileId\":\"default\","
        "\"profileName\":\"Default backup\","
        "\"state\":\"running\","
        "\"phase\":\"send\","
        "\"message\":\"copying\","
        "\"currentSourceName\":\"Home\","
        "\"updatedAt\":\"2026-08-23T12:00:00Z\""
        "}\n"
    );
    std::ostringstream output;

    btrfsbackup::command_status(root / "status", root / "history", {"--human"}, output);

    expect_contains("human status", output.str(), "Default backup: running\n");
    expect_contains("human phase", output.str(), "  phase: send\n");
    expect_contains("human message", output.str(), "  copying\n");
    expect_contains("human source", output.str(), "  source: Home\n");
    fs::remove_all(root);
}

void test_list_profiles_rejects_invalid_name() {
    fs::path root = test_root("bad-profile");
    write_file(root / "profiles.d" / "-bad.env", "");

    expect_validation_error(
        "invalid profile",
        [&] {
            std::ostringstream output;
            btrfsbackup::command_list_profiles(root / "profiles.d", root / "profiles", output);
        },
        "invalid profile id"
    );
    fs::remove_all(root);
}

void test_history_limit() {
    fs::path root = test_root("history-limit");
    write_file(root / "history" / "default" / "2026-08-22T000000Z.json", "{\"id\":1}");
    write_file(root / "history" / "default" / "2026-08-23T000000Z.json", "{\"id\":2}");
    write_file(root / "history" / "default" / "last.json", "{\"id\":3}");
    std::ostringstream output;

    btrfsbackup::command_history(root / "history", {"--limit", "1"}, output);

    expect_eq("history limit", output.str(), "[\n{\"id\":2}\n]\n");
    fs::remove_all(root);
}

void test_write_status_command_writes_current_and_history() {
    fs::path root = test_root("write-status");

    btrfsbackup::command_write_status(
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

    expect_eq("current exists", fs::is_regular_file(root / "status" / "default" / "current.json") ? "yes" : "no", "yes");
    expect_eq("history exists", fs::is_regular_file(root / "history" / "default" / "20260823T024407Z-4298-30158.json") ? "yes" : "no", "yes");
    expect_eq("last exists", fs::is_regular_file(root / "history" / "default" / "last.json") ? "yes" : "no", "yes");

    std::ostringstream output;
    btrfsbackup::command_status(root / "status", root / "history", {"--human"}, output);
    expect_contains("write human status", output.str(), "Default backup: succeeded\n");
    fs::remove_all(root);
}

void test_write_status_requires_target() {
    expect_validation_error(
        "write target",
        [&] {
            btrfsbackup::command_write_status(
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
    expect_validation_error(
        "write history finished",
        [&] {
            btrfsbackup::command_write_status(
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
    write_file(root / "main.env", "A=1\n");
    write_file(root / "10-root.conf", "SOURCE_NAME=root\n");
    write_file(root / "20-home.conf", "SOURCE_NAME=home\n");

    std::string digest = btrfsbackup::compute_config_fingerprint(
        "2.0.0",
        root / "main.env",
        {root / "10-root.conf", root / "20-home.conf"}
    );

    expect_eq("config fingerprint", digest, "f125982c7f64868550006c139bdba904248a93b4118afcc2332190e516494c34");

    std::ostringstream output;
    btrfsbackup::command_config_fingerprint(
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
    expect_eq("config fingerprint command", output.str(), digest + "\n");
    fs::remove_all(root);
}

void test_success_state_write_and_match() {
    fs::path root = test_root("success-state");
    fs::path state_dir = root / "state" / "profiles" / "default";

    btrfsbackup::command_write_success_state(
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
    expect_eq("success state exists", fs::is_regular_file(state_file) ? "yes" : "no", "yes");
    std::ifstream stream(state_file);
    std::string content{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    expect_contains("success state date", content, "date=2026-08-23\n");
    expect_contains("success state source count", content, "source_count=2\n");
    expect_true(
        "success state match",
        btrfsbackup::last_success_matches(
            state_dir,
            "2026-08-23",
            "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
            "630b159cf7939e5baf76ce27d4505a5cf68fe2995d5071c1f22e011e143b7c67"
        ),
        "expected last success to match"
    );
    expect_true(
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
    btrfsbackup::command_check_last_success(
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
    expect_eq("success state command", output.str(), "yes\n");
    fs::remove_all(root);
}

void test_pending_marker_write_read_and_clear() {
    fs::path root = test_root("pending-marker");
    fs::path state_dir = root / "state" / "profiles" / "default";
    fs::path snapshot = root / "local" / "root" / "root-2026-08-23T082504";

    btrfsbackup::command_write_pending_marker(
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
    expect_eq("pending marker exists", fs::is_regular_file(marker) ? "yes" : "no", "yes");
    std::ifstream stream(marker);
    std::string content{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    expect_contains("pending source", content, "source_name=root\n");
    expect_contains("pending run", content, "run_id=20260823T062504Z-123-456\n");

    std::ostringstream output;
    btrfsbackup::command_read_pending_marker(
        {
            "--marker",
            marker.string(),
            "--field",
            "local_snapshot_path",
        },
        output
    );
    expect_eq("pending path", output.str(), snapshot.string() + "\n");

    btrfsbackup::command_clear_pending_marker(
        {
            "--marker",
            marker.string(),
            "--profile-state-dir",
            state_dir.string(),
        }
    );
    expect_eq("pending marker cleared", fs::exists(marker) ? "yes" : "no", "no");
    fs::remove_all(root);
}

void test_migrate_legacy_state_moves_unclaimed_files() {
    fs::path root = test_root("legacy-state");
    fs::path state_dir = root / "state";
    fs::path profile_state_dir = state_dir / "profiles" / "default";
    write_file(state_dir / "last-success", "profile_id=legacy\n");
    write_file(state_dir / "pending-root", "local_snapshot_path=/snapshots/root-old\n");
    write_file(state_dir / "pending-home", "local_snapshot_path=/snapshots/home-old\n");
    write_file(profile_state_dir / "pending-home", "local_snapshot_path=/snapshots/home-current\n");

    btrfsbackup::command_migrate_legacy_state(
        {
            "--state-dir",
            state_dir.string(),
            "--profile-state-dir",
            profile_state_dir.string(),
        }
    );

    expect_eq("legacy success moved", fs::is_regular_file(profile_state_dir / "last-success") ? "yes" : "no", "yes");
    expect_eq("legacy success removed", fs::exists(state_dir / "last-success") ? "yes" : "no", "no");
    expect_eq("legacy pending moved", fs::is_regular_file(profile_state_dir / "pending-root") ? "yes" : "no", "yes");
    expect_eq("legacy pending removed", fs::exists(state_dir / "pending-root") ? "yes" : "no", "no");
    expect_eq("conflicting pending kept", fs::is_regular_file(state_dir / "pending-home") ? "yes" : "no", "yes");

    std::ifstream stream(profile_state_dir / "pending-home");
    std::string content{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    expect_contains("current pending preserved", content, "/snapshots/home-current");
    fs::remove_all(root);
}

void test_parse_profile_sources_from_json() {
    fs::path root = test_root("profile-sources");
    btrfsbackup::Json profile = {
        {"schemaVersion", 1},
        {"profileId", "default"},
        {"name", "Default backup"},
        {"enabled", true},
        {"target", {
            {"device", (root / "dev/disk/by-uuid/11111111-2222-3333-4444-555555555555").string()},
            {"luksUuid", "11111111-2222-3333-4444-555555555555"},
            {"mapperName", "backupdisk"},
            {"mountPoint", "/mnt/backup"}
        }},
        {"sources", btrfsbackup::Json::array({
            {
                {"id", "root"},
                {"name", "Root"},
                {"enabled", true},
                {"subvolume", "/mnt/source/root"},
                {"localSnapshotDir", "/mnt/source/.snapshots/root"},
                {"remoteSubdir", "root"},
                {"remoteRetention", 7},
                {"localRetention", 3}
            },
            {
                {"id", "home"},
                {"name", "Home"},
                {"enabled", false},
                {"subvolume", "/mnt/source/home"},
                {"localSnapshotDir", "/mnt/source/.snapshots/home"},
                {"remoteSubdir", "home"},
                {"remoteRetention", 7},
                {"localRetention", 3}
            }
        })}
    };
    fs::path profile_json = root / "profile.json";
    write_file(profile_json, btrfsbackup::dump_json(profile));

    std::ostringstream output;
    btrfsbackup::command_parse_profile_sources(
        {
            "--file",
            profile_json.string(),
        },
        output
    );

    expect_eq(
        "profile sources",
        output.str(),
        "root\n/mnt/source/root\n/mnt/source/.snapshots/root\nroot\n7\n3\n"
    );
    fs::remove_all(root);
}

} // namespace

int main() {
    test_history_without_directory_returns_empty_array();
    test_status_falls_back_to_last_json();
    test_list_profiles_from_json_and_env_files();
    test_status_human_format();
    test_list_profiles_rejects_invalid_name();
    test_history_limit();
    test_write_status_command_writes_current_and_history();
    test_write_status_requires_target();
    test_write_history_requires_finished_at();
    test_config_fingerprint_matches_legacy_stream();
    test_success_state_write_and_match();
    test_pending_marker_write_read_and_clear();
    test_migrate_legacy_state_moves_unclaimed_files();
    test_parse_profile_sources_from_json();

    if (failures > 0) {
        return 1;
    }
    std::cout << "ok - ctl module tests\n";
    return 0;
}
