#include <filesystem>
#include <sstream>
#include <string>

#include <btrfsbackup/command/profile_list_command.hpp>
#include <btrfsbackup/command/status_history_command.hpp>
#include <btrfsbackup/command/status_command.hpp>
#include <btrfsbackup/command/status_show_command.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/status_writer.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("status-history", name);
}

void test_history_without_directory_returns_empty_array() {
    fs::path root = test_root("history-empty");
    std::ostringstream output;

    btrfsbackup::command::status_history(root / "history", {}, output);

    test_helpers::expect_eq("history without directory", output.str(), "[]\n");
    fs::remove_all(root);
}

void test_status_falls_back_to_last_json() {
    fs::path root = test_root("status-fallback");
    test_helpers::write_file(root / "history" / "default" / "last.json", "{\"profileId\":\"default\",\"state\":\"ok\"}\n");
    std::ostringstream output;

    btrfsbackup::command::status_show(root / "status", root / "history", {}, output);

    test_helpers::expect_eq("status fallback", output.str(), "{\"profileId\":\"default\",\"state\":\"ok\"}\n");
    fs::remove_all(root);
}

void test_list_profiles_from_json_files() {
    fs::path root = test_root("profiles");
    test_helpers::write_file(root / "profiles" / "beta" / "profile.json", "{}\n");
    test_helpers::write_file(root / "profiles" / "default" / "profile.json", "{}\n");
    std::ostringstream output;

    btrfsbackup::command::profile_list(root / "profiles.d", root / "profiles", output);

    test_helpers::expect_eq("list profiles", output.str(), "beta\ndefault\n");
    fs::remove_all(root);
}

void test_status_human_format() {
    fs::path root = test_root("human");
    test_helpers::write_file(
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

    btrfsbackup::command::status_show(root / "status", root / "history", {"--human"}, output);

    test_helpers::expect_contains("human status", output.str(), "Default backup: running\n");
    test_helpers::expect_contains("human phase", output.str(), "  phase: send\n");
    test_helpers::expect_contains("human message", output.str(), "  copying\n");
    test_helpers::expect_contains("human source", output.str(), "  source: Home\n");
    fs::remove_all(root);
}

std::vector<std::string> required_status_api_fields() {
    return {
        "schemaVersion",
        "profileId",
        "profileName",
        "runId",
        "state",
        "phase",
        "message",
        "currentSourceName",
        "sourceIndex",
        "sourceCount",
        "startedAt",
        "updatedAt",
        "finishedAt",
        "errorCode",
        "errorMessage",
        "details",
        "recoverable",
        "suggestedAction",
        "canCancel",
        "bytesProcessed",
        "bytesTotalEstimated",
        "runBytesProcessed",
        "speedBps",
        "etaSeconds",
        "sourceProgress",
        "overallProgress",
        "progressAccuracy",
        "exitCode",
    };
}

btrfsbackup::RunStatusRecord watch_sample_record() {
    return {
        .profile_id = "default",
        .profile_name = "Default backup",
        .run_id = "20260823T024407Z-4298-30158",
        .state = "running",
        .phase = "transferring",
        .message = "Backup transfer is running.",
        .current_source_name = "home",
        .source_index = 1,
        .source_count = 2,
        .started_at = "2026-08-23T02:44:07Z",
        .updated_at = "2026-08-23T02:45:07Z",
        .can_cancel = true,
        .bytes_processed = 4096,
        .bytes_total_estimated = 8192,
        .run_bytes_processed = 12288,
        .speed_bps = 2048,
        .eta_seconds = 2,
        .source_progress = 50,
        .overall_progress = 0,
        .progress_accuracy = "estimated",
    };
}

void test_status_watch_json_emits_status_api_shape_once() {
    fs::path root = test_root("watch-json");
    btrfsbackup::write_current_status(root / "status", watch_sample_record());

    std::ostringstream output;
    std::string previous;
    bool emitted = btrfsbackup::command::status_watch_once(
        root / "status",
        {"--profile", "default", "--json"},
        previous,
        output
    );

    test_helpers::expect_true("watch emitted", emitted, "watch should emit current status");
    btrfsbackup::Json data = btrfsbackup::Json::parse(output.str());
    for (const std::string& field : required_status_api_fields()) {
        test_helpers::expect_true("watch field " + field, data.contains(field), "missing field " + field);
    }
    test_helpers::expect_true("watch schema", data.at("schemaVersion") == 2, "wrong schema");
    test_helpers::expect_true("watch profile", data.at("profileId") == "default", "wrong profile");
    test_helpers::expect_true("watch progress", data.at("sourceProgress") == 50, "wrong source progress");

    std::ostringstream duplicate_output;
    bool duplicate = btrfsbackup::command::status_watch_once(
        root / "status",
        {"--profile", "default", "--json"},
        previous,
        duplicate_output
    );
    test_helpers::expect_true("watch duplicate", !duplicate, "unchanged status should not be emitted twice");
    test_helpers::expect_eq("watch duplicate output", duplicate_output.str(), "");

    fs::remove_all(root);
}

void test_list_profiles_rejects_invalid_name() {
    fs::path root = test_root("bad-profile");
    test_helpers::write_file(root / "profiles" / "-bad" / "profile.json", "{}\n");

    test_helpers::expect_validation_error(
        "invalid profile",
        [&] {
            std::ostringstream output;
            btrfsbackup::command::profile_list(root / "profiles.d", root / "profiles", output);
        },
        "invalid profile id"
    );
    fs::remove_all(root);
}

void test_history_limit() {
    fs::path root = test_root("history-limit");
    test_helpers::write_file(root / "history" / "default" / "2026-08-22T000000Z.json", "{\"id\":1}");
    test_helpers::write_file(root / "history" / "default" / "2026-08-23T000000Z.json", "{\"id\":2}");
    test_helpers::write_file(root / "history" / "default" / "last.json", "{\"id\":3}");
    std::ostringstream output;

    btrfsbackup::command::status_history(root / "history", {"--limit", "1"}, output);

    test_helpers::expect_eq("history limit", output.str(), "[\n{\"id\":2}\n]\n");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_history_without_directory_returns_empty_array();
    test_status_falls_back_to_last_json();
    test_list_profiles_from_json_files();
    test_status_human_format();
    test_status_watch_json_emits_status_api_shape_once();
    test_list_profiles_rejects_invalid_name();
    test_history_limit();

    return test_helpers::finish("status/history tests");
}
