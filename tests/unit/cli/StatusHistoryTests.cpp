// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <sstream>
#include <string>

#include <cli/ProfileListCommand.hpp>
#include <cli/StatusHistoryCommand.hpp>
#include <cli/StatusCommand.hpp>
#include <cli/StatusShowCommand.hpp>
#include <config/model/JsonIo.hpp>
#include <core/RuntimeTime.hpp>
#include <state/StatusWriter.hpp>
#include <platform/linux/filesystem/PosixDurableFileOperations.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("status-history", name);
}

void test_history_without_directory_returns_empty_array() {
    fs::path root = test_root("history-empty");
    std::ostringstream output;

    btrfsbackup::cli::status_history(root / "history", {}, output);

    test_helpers::expect_eq("history without directory", output.str(), "[]\n");
    fs::remove_all(root);
}

void test_status_falls_back_to_last_json() {
    fs::path root = test_root("status-fallback");
    test_helpers::write_file(root / "history" / "default" / "last.json", "{\"profileId\":\"default\",\"state\":\"ok\"}\n");
    std::ostringstream output;

    btrfsbackup::cli::status_show(root / "status", root / "history", {}, output);

    test_helpers::expect_eq("status fallback", output.str(), "{\"profileId\":\"default\",\"state\":\"ok\"}\n");
    fs::remove_all(root);
}

void test_list_profiles_from_json_files() {
    fs::path root = test_root("profiles");
    test_helpers::write_file(root / "profiles" / "beta" / "profile.json", "{}\n");
    test_helpers::write_file(root / "profiles" / "default" / "profile.json", "{}\n");
    std::ostringstream output;

    btrfsbackup::cli::profile_list(root / "profiles", output);

    test_helpers::expect_eq("list profiles", output.str(), "beta\ndefault\n");
    fs::remove_all(root);
}

void test_public_status_human_format() {
    fs::path root = test_root("public-human");
    test_helpers::write_file(
        root / "status" / "default" / "current.json",
        "{"
        "\"schemaVersion\":3,"
        "\"state\":\"running\","
        "\"errorCode\":\"\","
        "\"sourceName\":\"Home\","
        "\"targetName\":\"backupdisk\","
        "\"speedBps\":0,"
        "\"etaSeconds\":-1,"
        "\"sourceProgress\":50,"
        "\"overallProgress\":25,"
        "\"progressAccuracy\":\"estimated\""
        "}\n"
    );
    std::ostringstream output;

    btrfsbackup::cli::status_show(root / "status", root / "history", {"--human"}, output);

    test_helpers::expect_contains("public human status", output.str(), "default: running\n");
    test_helpers::expect_contains("public human source", output.str(), "  source: Home\n");
    test_helpers::expect_contains("public human target", output.str(), "  target: backupdisk\n");
    fs::remove_all(root);
}

void test_private_history_human_format() {
    fs::path root = test_root("private-human");
    test_helpers::write_file(
        root / "history" / "default" / "last.json",
        "{"
        "\"schemaVersion\":2,"
        "\"profileId\":\"default\","
        "\"profileName\":\"Default backup\","
        "\"state\":\"succeeded\","
        "\"phase\":\"completed\","
        "\"message\":\"backup completed\""
        "}\n"
    );
    std::ostringstream output;

    btrfsbackup::cli::status_show(root / "status", root / "history", {"--human"}, output);

    test_helpers::expect_contains("private human status", output.str(), "Default backup: succeeded\n");
    test_helpers::expect_contains("private human phase", output.str(), "  phase: completed\n");
    test_helpers::expect_contains("private human message", output.str(), "  backup completed\n");
    fs::remove_all(root);
}

std::vector<std::string> required_status_api_fields() {
    return {
        "schemaVersion",
        "state",
        "errorCode",
        "sourceName",
        "targetName",
        "speedBps",
        "etaSeconds",
        "sourceProgress",
        "overallProgress",
        "progressAccuracy",
    };
}

btrfsbackup::state::RunStatus watch_sample_record() {
    return {
        .profile_id = btrfsbackup::ProfileId{"default"},
        .profile_name = "Default backup",
        .run_id = btrfsbackup::RunId{"20260823T024407Z-4298-30158"},
        .state = btrfsbackup::state::RunState::Running,
        .phase = btrfsbackup::state::RunPhase::Transferring,
        .message = "Backup transfer is running.",
        .current_source_name = "home",
        .source_index = 1,
        .source_count = 2,
        .started_at = *btrfsbackup::parse_utc_timestamp("2026-08-23T02:44:07Z"),
        .updated_at = *btrfsbackup::parse_utc_timestamp("2026-08-23T02:45:07Z"),
        .can_cancel = true,
        .progress = btrfsbackup::state::RunProgress{
            .processed_bytes = 4096,
            .estimated_bytes = 8192,
            .run_processed_bytes = 12288,
            .speed_bps = 2048,
            .eta_seconds = 2,
            .source_percent = 50,
            .overall_percent = 0,
            .accuracy = btrfsbackup::state::ProgressAccuracy::Estimated,
        },
    };
}

void test_status_watch_json_emits_status_api_shape_once() {
    fs::path root = test_root("watch-json");
    btrfsbackup::platform::linux::filesystem::PosixDurableFileOperations durable_files;
    btrfsbackup::state::write_current_status(durable_files, root / "status", watch_sample_record());

    std::ostringstream output;
    std::string previous;
    bool emitted = btrfsbackup::cli::status_watch_once(
        root / "status",
        {"--profile", "default"},
        previous,
        output
    );

    test_helpers::expect_true("watch emitted", emitted, "watch should emit current status");
    btrfsbackup::config::Json data = btrfsbackup::config::Json::parse(output.str());
    for (const std::string& field : required_status_api_fields()) {
        test_helpers::expect_true("watch field " + field, data.contains(field), "missing field " + field);
    }
    test_helpers::expect_true("watch schema", data.at("schemaVersion") == 3, "wrong schema");
    test_helpers::expect_true("watch profile hidden", !data.contains("profileId"), "public status exposes profile id");
    test_helpers::expect_true("watch details hidden", !data.contains("details"), "public status exposes details");
    test_helpers::expect_true("watch progress", data.at("sourceProgress") == 50, "wrong source progress");

    std::ostringstream duplicate_output;
    bool duplicate = btrfsbackup::cli::status_watch_once(
        root / "status",
        {"--profile", "default"},
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
            btrfsbackup::cli::profile_list(root / "profiles", output);
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

    btrfsbackup::cli::status_history(root / "history", {"--limit", "1"}, output);

    test_helpers::expect_eq("history limit", output.str(), "[\n{\"id\":2}\n]\n");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_history_without_directory_returns_empty_array();
    test_status_falls_back_to_last_json();
    test_list_profiles_from_json_files();
    test_public_status_human_format();
    test_private_history_human_format();
    test_status_watch_json_emits_status_api_shape_once();
    test_list_profiles_rejects_invalid_name();
    test_history_limit();

    return test_helpers::finish("status/history tests");
}
