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

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/history.hpp>
#include <btrfsbackup/profile_list.hpp>
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

void test_list_profiles_from_env_files() {
    fs::path root = test_root("profiles");
    write_file(root / "profiles.d" / "alpha.env", "");
    write_file(root / "profiles.d" / "default.env", "");
    std::ostringstream output;

    btrfsbackup::command_list_profiles(root / "profiles.d", root / "backup.env", output);

    expect_eq("list profiles", output.str(), "alpha\ndefault\n");
    fs::remove_all(root);
}

void test_list_profiles_legacy_fallback() {
    fs::path root = test_root("legacy");
    write_file(root / "backup.env", "BACKUP_DEVICE=/dev/test\n");
    std::ostringstream output;

    btrfsbackup::command_list_profiles(root / "profiles.d", root / "backup.env", output);

    expect_eq("legacy fallback", output.str(), "default (legacy)\n");
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
            btrfsbackup::command_list_profiles(root / "profiles.d", root / "backup.env", output);
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

} // namespace

int main() {
    test_history_without_directory_returns_empty_array();
    test_status_falls_back_to_last_json();
    test_list_profiles_from_env_files();
    test_list_profiles_legacy_fallback();
    test_status_human_format();
    test_list_profiles_rejects_invalid_name();
    test_history_limit();
    test_write_status_command_writes_current_and_history();
    test_write_status_requires_target();
    test_write_history_requires_finished_at();

    if (failures > 0) {
        return 1;
    }
    std::cout << "ok - ctl module tests\n";
    return 0;
}
