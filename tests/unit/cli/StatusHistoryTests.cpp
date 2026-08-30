// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <sstream>
#include <string>

#include <cli/profile/ProfileListCommand.hpp>
#include <cli/status/StatusHistoryCommand.hpp>
#include <cli/status/StatusCommand.hpp>
#include <cli/status/StatusShowCommand.hpp>
#include <config/json/JsonIo.hpp>
#include <core/RuntimeTime.hpp>
#include <state/persistence/StatusWriter.hpp>
#include <platform/linux/filesystem/PosixDurableFileOperations.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("status-history", name);
}

std::string private_history(const std::string& run_id, const std::string& message = "") {
    return btrfsbackup::config::json::Json({
                                               {"schemaVersion", 2},
                                               {"profileId", "default"},
                                               {"profileName", "Default backup"},
                                               {"runId", run_id},
                                               {"state", "succeeded"},
                                               {"phase", "succeeded"},
                                               {"message", message},
                                               {"currentSourceName", "Home"},
                                               {"targetName", "backupdisk"},
                                               {"sourceIndex", 1},
                                               {"sourceCount", 1},
                                               {"startedAt", "2026-08-23T00:00:00Z"},
                                               {"updatedAt", "2026-08-23T00:01:00Z"},
                                               {"finishedAt", "2026-08-23T00:01:00Z"},
                                               {"errorCode", ""},
                                               {"errorMessage", ""},
                                               {"details", btrfsbackup::config::json::Json::object()},
                                               {"recoverable", false},
                                               {"suggestedAction", ""},
                                               {"canCancel", false},
                                               {"bytesProcessed", 0},
                                               {"bytesTotalEstimated", 0},
                                               {"runBytesProcessed", 0},
                                               {"speedBps", 0},
                                               {"etaSeconds", -1},
                                               {"sourceProgress", 100},
                                               {"overallProgress", 100},
                                               {"progressAccuracy", "exact"},
                                               {"exitCode", 0},
                                           })
        .dump();
}

void test_history_without_directory_returns_empty_array() {
    fs::path root = test_root("history-empty");
    std::ostringstream output;

    btrfsbackup::cli::status::status_history(root / "history", {}, output);

    test_helpers::expect_eq("history without directory", output.str(), "[]\n");
    fs::remove_all(root);
}

void test_status_falls_back_to_last_json() {
    fs::path root = test_root("status-fallback");
    const std::string history = private_history("run-1");
    test_helpers::write_file(root / "history" / "default" / "last.json", history + "\n");
    std::ostringstream output;

    btrfsbackup::cli::status::status_show(root / "status", root / "history", {}, output);

    test_helpers::expect_eq("status fallback", output.str(), history + "\n");
    fs::remove_all(root);
}

void test_list_profiles_from_json_files() {
    fs::path root = test_root("profiles");
    test_helpers::write_file(root / "profiles" / "beta" / "profile.json", "{}\n");
    test_helpers::write_file(root / "profiles" / "default" / "profile.json", "{}\n");
    std::ostringstream output;

    btrfsbackup::cli::profile::profile_list(root / "profiles", output);

    test_helpers::expect_eq("list profiles", output.str(), "beta\ndefault\n");
    fs::remove_all(root);
}

void test_public_status_human_format() {
    fs::path root = test_root("public-human");
    test_helpers::write_file(
        root / "status" / "default" / "current.json",
        "{"
        "\"schemaVersion\":3,"
        "\"runId\":\"run-1\","
        "\"state\":\"running\","
        "\"phase\":\"transferring\","
        "\"activity\":\"transferring\","
        "\"canCancel\":true,"
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

    btrfsbackup::cli::status::status_show(root / "status", root / "history", {"--human"}, output);

    test_helpers::expect_contains("public human status", output.str(), "default: running\n");
    test_helpers::expect_contains("public human source", output.str(), "  source: Home\n");
    test_helpers::expect_contains("public human target", output.str(), "  target: backupdisk\n");
    fs::remove_all(root);
}

void test_private_history_human_format() {
    fs::path root = test_root("private-human");
    test_helpers::write_file(
        root / "history" / "default" / "last.json",
        private_history("run-1", "backup completed") + "\n"
    );
    std::ostringstream output;

    btrfsbackup::cli::status::status_show(root / "status", root / "history", {"--human"}, output);

    test_helpers::expect_contains("private human status", output.str(), "Default backup: succeeded\n");
    test_helpers::expect_contains("private human phase", output.str(), "  phase: succeeded\n");
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
    bool emitted = btrfsbackup::cli::status::status_watch_once(
        root / "status",
        {"--profile", "default"},
        previous,
        output
    );

    test_helpers::expect_true("watch emitted", emitted, "watch should emit current status");
    btrfsbackup::config::json::Json data = btrfsbackup::config::json::Json::parse(output.str());
    for (const std::string& field : required_status_api_fields()) {
        test_helpers::expect_true("watch field " + field, data.contains(field), "missing field " + field);
    }
    test_helpers::expect_true("watch schema", data.at("schemaVersion") == 3, "wrong schema");
    test_helpers::expect_true("watch profile hidden", !data.contains("profileId"), "public status exposes profile id");
    test_helpers::expect_true("watch details hidden", !data.contains("details"), "public status exposes details");
    test_helpers::expect_true("watch progress", data.at("sourceProgress") == 50, "wrong source progress");

    std::ostringstream duplicate_output;
    bool duplicate = btrfsbackup::cli::status::status_watch_once(
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
            btrfsbackup::cli::profile::profile_list(root / "profiles", output);
        },
        "invalid profile id"
    );
    fs::remove_all(root);
}

void test_history_limit() {
    fs::path root = test_root("history-limit");
    const std::string older = private_history("run-1");
    const std::string newer = private_history("run-2");
    test_helpers::write_file(root / "history" / "default" / "2026-08-22T000000Z.json", older);
    test_helpers::write_file(root / "history" / "default" / "2026-08-23T000000Z.json", newer);
    test_helpers::write_file(root / "history" / "default" / "last.json", newer);
    std::ostringstream output;

    btrfsbackup::cli::status::status_history(root / "history", {"--limit", "1"}, output);

    test_helpers::expect_eq("history limit", output.str(), "[\n" + newer + "\n]\n");
    fs::remove_all(root);
}

void test_status_rejects_oversized_document() {
    fs::path root = test_root("status-oversized");
    test_helpers::write_file(
        root / "status" / "default" / "current.json",
        std::string(1024 * 1024 + 1, 'x')
    );

    test_helpers::expect_validation_error(
        "oversized CLI status",
        [&] {
            std::ostringstream output;
            btrfsbackup::cli::status::status_show(root / "status", root / "history", {}, output);
        },
        "exceeds the size limit"
    );
    fs::remove_all(root);
}

void test_status_rejects_symbolic_link() {
    fs::path root = test_root("status-symlink");
    const fs::path target = root / "status.json";
    const fs::path current = root / "status" / "default" / "current.json";
    test_helpers::write_file(target, private_history("run-1"));
    fs::create_directories(current.parent_path());
    fs::create_symlink(target, current);

    test_helpers::expect_validation_error(
        "CLI status symlink",
        [&] {
            std::ostringstream output;
            btrfsbackup::cli::status::status_show(root / "status", root / "history", {}, output);
        },
        "cannot read document"
    );
    fs::remove_all(root);
}

void test_history_ignores_symbolic_links() {
    fs::path root = test_root("history-symlink");
    const fs::path target = root / "outside.json";
    const fs::path link = root / "history" / "default" / "2026-08-23T000000Z.json";
    test_helpers::write_file(target, private_history("run-1"));
    fs::create_directories(link.parent_path());
    fs::create_symlink(target, link);
    std::ostringstream output;

    btrfsbackup::cli::status::status_history(root / "history", {}, output);

    test_helpers::expect_eq("history symlink", output.str(), "[]\n");
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
    test_status_rejects_oversized_document();
    test_status_rejects_symbolic_link();
    test_history_ignores_symbolic_links();

    return test_helpers::finish("status/history tests");
}
