// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <core/errors.hpp>
#include <core/identifiers.hpp>
#include <config/model/json.hpp>
#include <config/model/json_io.hpp>
#include <state/status_writer.hpp>
#include <state/run_history.hpp>
#include <platform/linux/file_io.hpp>

namespace fs = std::filesystem;

namespace {

btrfsbackup::PosixDurableFileOperations& durable_files() {
    static btrfsbackup::PosixDurableFileOperations files;
    return files;
}

using btrfsbackup::Json;
using btrfsbackup::ErrorCode;
using btrfsbackup::ProgressAccuracy;
using btrfsbackup::RunError;
using btrfsbackup::RunPhase;
using btrfsbackup::RunState;
using btrfsbackup::RunStatus;
using btrfsbackup::SuggestedAction;
using btrfsbackup::ValidationError;

int failures = 0;

void fail(const std::string& name, const std::string& message) {
    ++failures;
    std::cerr << "not ok - " << name << ": " << message << '\n';
}

void expect_true(const std::string& name, bool condition, const std::string& message) {
    if (!condition) {
        fail(name, message);
    }
}

void expect_eq(const std::string& name, const std::string& actual, const std::string& expected) {
    if (actual != expected) {
        fail(name, "expected [" + expected + "], got [" + actual + "]");
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
    fs::path root = fs::temp_directory_path() / ("btrfsbackup-status-writer-tests-" + std::to_string(getpid()) + "-" + name);
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

RunStatus sample_record() {
    return {
        .profile_id = btrfsbackup::ProfileId{"default"},
        .profile_name = "Default backup",
        .run_id = btrfsbackup::RunId{"20260823T024407Z-4298-30158"},
        .state = RunState::Succeeded,
        .phase = RunPhase::Succeeded,
        .message = "Backup completed.",
        .current_source_name = "Home",
        .target_name = "backupdisk",
        .source_index = 1,
        .source_count = 2,
        .started_at = "2026-08-23T02:44:07+00:00",
        .updated_at = "2026-08-23T02:45:07+00:00",
        .finished_at = "2026-08-23T02:45:07+00:00",
        .exit_code = 0,
    };
}

int mode_of(const fs::path& path) {
    struct stat info{};
    if (stat(path.c_str(), &info) != 0) {
        fail("stat", "cannot stat " + path.string());
        return 0;
    }
    return info.st_mode & 0777;
}

void test_build_status_json_matches_contract_shape() {
    Json data = btrfsbackup::build_status_json(sample_record());

    expect_true("schema", data.at("schemaVersion") == 2, "wrong schemaVersion");
    expect_true("profile id", data.at("profileId") == "default", "wrong profileId");
    expect_true("profile name", data.at("profileName") == "Default backup", "wrong profileName");
    expect_true("run id", data.at("runId") == "20260823T024407Z-4298-30158", "wrong runId");
    expect_true("state", data.at("state") == "succeeded", "wrong state");
    expect_true("phase", data.at("phase") == "succeeded", "wrong phase");
    expect_true("message", data.at("message") == "Backup completed.", "wrong message");
    expect_true("source", data.at("currentSourceName") == "Home", "wrong currentSourceName");
    expect_true("target", data.at("targetName") == "backupdisk", "wrong targetName");
    expect_true("source index", data.at("sourceIndex") == 1, "wrong sourceIndex");
    expect_true("source count", data.at("sourceCount") == 2, "wrong sourceCount");
    expect_true("started", data.at("startedAt") == "2026-08-23T02:44:07+00:00", "wrong startedAt");
    expect_true("updated", data.at("updatedAt") == "2026-08-23T02:45:07+00:00", "wrong updatedAt");
    expect_true("finished", data.at("finishedAt") == "2026-08-23T02:45:07+00:00", "wrong finishedAt");
    expect_true("error code", data.at("errorCode") == "", "wrong errorCode");
    expect_true("error message", data.at("errorMessage") == "", "wrong errorMessage");
    expect_true("details", data.at("details").is_object() && data.at("details").empty(), "wrong details");
    expect_true("recoverable", data.at("recoverable") == false, "wrong recoverable");
    expect_true("suggested action", data.at("suggestedAction") == "", "wrong suggestedAction");
    expect_true("can cancel", data.at("canCancel") == false, "wrong canCancel");
    expect_true("target state absent", !data.contains("safeToRemove"), "run status must not expose target removal state");
    expect_true("bytes processed", data.at("bytesProcessed") == 0, "wrong bytesProcessed");
    expect_true("bytes total estimated", data.at("bytesTotalEstimated") == 0, "wrong bytesTotalEstimated");
    expect_true("run bytes processed", data.at("runBytesProcessed") == 0, "wrong runBytesProcessed");
    expect_true("speed", data.at("speedBps") == 0, "wrong speedBps");
    expect_true("eta", data.at("etaSeconds") == -1, "wrong etaSeconds");
    expect_true("source progress", data.at("sourceProgress") == -1, "wrong sourceProgress");
    expect_true("overall progress", data.at("overallProgress") == -1, "wrong overallProgress");
    expect_true("progress accuracy", data.at("progressAccuracy") == "indeterminate", "wrong progressAccuracy");
    expect_true("exit", data.at("exitCode") == 0, "wrong exitCode");
}

void test_build_status_json_includes_structured_error() {
    RunStatus record = sample_record();
    record.state = RunState::Failed;
    record.phase = RunPhase::ValidatingTarget;
    record.message = "Validation failed.";
    record.error = RunError{
        .code = ErrorCode::TargetBtrfsUuidMismatch,
        .message = "Target Btrfs UUID does not match.",
        .recoverable = false,
        .suggested_action = SuggestedAction{"connect-correct-target"},
    };
    record.details = {
        {"expected", std::string{"expected-uuid"}},
        {"actual", std::string{"actual-uuid"}},
    };
    record.can_cancel = true;
    record.exit_code = 2;

    Json data = btrfsbackup::build_status_json(record);

    expect_true("structured error code", data.at("errorCode") == "target.btrfs_uuid_mismatch", "wrong error code");
    expect_true("structured error message", data.at("errorMessage") == "Target Btrfs UUID does not match.", "wrong error message");
    expect_true("structured detail expected", data.at("details").at("expected") == "expected-uuid", "wrong expected detail");
    expect_true("structured detail actual", data.at("details").at("actual") == "actual-uuid", "wrong actual detail");
    expect_true("structured recoverable", data.at("recoverable") == false, "wrong recoverable");
    expect_true("structured action", data.at("suggestedAction") == "connect-correct-target", "wrong suggested action");
    expect_true("structured can cancel", data.at("canCancel") == true, "wrong canCancel");
}

void test_build_public_status_json_excludes_diagnostics() {
    RunStatus record = sample_record();
    record.state = RunState::Failed;
    record.error = RunError{
        .code = ErrorCode::TargetBtrfsUuidMismatch,
        .message = "Target Btrfs UUID does not match.",
    };
    record.details = {
        {"expected", std::string{"expected-uuid"}},
        {"actual", std::string{"actual-uuid"}},
    };
    record.progress.speed_bps = 2048;
    record.progress.eta_seconds = 12;
    record.progress.source_percent = 50;
    record.progress.overall_percent = 25;
    record.progress.accuracy = ProgressAccuracy::Estimated;

    Json data = btrfsbackup::build_public_status_json(record);

    expect_true("public schema", data.at("schemaVersion") == 3, "wrong public schemaVersion");
    expect_true("public state", data.at("state") == "failed", "wrong public state");
    expect_true("public generic error", data.at("errorCode") == "backup.failed", "error code is not generic");
    expect_true("public source", data.at("sourceName") == "Home", "wrong public source name");
    expect_true("public target", data.at("targetName") == "backupdisk", "wrong public target name");
    expect_true("public speed", data.at("speedBps") == 2048, "wrong public speed");
    expect_true("public eta", data.at("etaSeconds") == 12, "wrong public ETA");
    expect_true("public source progress", data.at("sourceProgress") == 50, "wrong public source progress");
    expect_true("public overall progress", data.at("overallProgress") == 25, "wrong public overall progress");
    expect_true("public accuracy", data.at("progressAccuracy") == "estimated", "wrong public progress accuracy");
    for (const char* field : {"profileId", "profileName", "runId", "phase", "message", "currentSourceName", "startedAt", "updatedAt", "finishedAt", "errorMessage", "details", "recoverable", "suggestedAction", "exitCode"}) {
        expect_true(std::string("public excludes ") + field, !data.contains(field), std::string("public status exposes ") + field);
    }
}

void test_dump_status_json_uses_stable_order_and_newline() {
    std::string dumped = btrfsbackup::dump_status_json(sample_record());

    expect_eq(
        "dump",
        dumped,
        "{\n"
        "  \"schemaVersion\": 2,\n"
        "  \"profileId\": \"default\",\n"
        "  \"profileName\": \"Default backup\",\n"
        "  \"runId\": \"20260823T024407Z-4298-30158\",\n"
        "  \"state\": \"succeeded\",\n"
        "  \"phase\": \"succeeded\",\n"
        "  \"message\": \"Backup completed.\",\n"
        "  \"currentSourceName\": \"Home\",\n"
        "  \"targetName\": \"backupdisk\",\n"
        "  \"sourceIndex\": 1,\n"
        "  \"sourceCount\": 2,\n"
        "  \"startedAt\": \"2026-08-23T02:44:07+00:00\",\n"
        "  \"updatedAt\": \"2026-08-23T02:45:07+00:00\",\n"
        "  \"finishedAt\": \"2026-08-23T02:45:07+00:00\",\n"
        "  \"errorCode\": \"\",\n"
        "  \"errorMessage\": \"\",\n"
        "  \"details\": {},\n"
        "  \"recoverable\": false,\n"
        "  \"suggestedAction\": \"\",\n"
        "  \"canCancel\": false,\n"
        "  \"bytesProcessed\": 0,\n"
        "  \"bytesTotalEstimated\": 0,\n"
        "  \"runBytesProcessed\": 0,\n"
        "  \"speedBps\": 0,\n"
        "  \"etaSeconds\": -1,\n"
        "  \"sourceProgress\": -1,\n"
        "  \"overallProgress\": -1,\n"
        "  \"progressAccuracy\": \"indeterminate\",\n"
        "  \"exitCode\": 0\n"
        "}\n"
    );
}

void test_write_current_status() {
    fs::path root = test_root("current");
    fs::path status_root = root / "run" / "profiles";

    btrfsbackup::write_current_status(durable_files(), status_root, sample_record());

    fs::path current = status_root / "default" / "current.json";
    Json data = btrfsbackup::load_json_file(current);
    expect_true("current exists", fs::is_regular_file(current), "missing current.json");
    expect_true("current state", data.at("state") == "succeeded", "wrong current state");
    expect_true("current public schema", data.at("schemaVersion") == 3, "wrong current schema");
    expect_true("current diagnostics absent", !data.contains("runId") && !data.contains("details"), "current status exposes diagnostics");
    expect_true("current mode", mode_of(current) == 0644, "current.json should be 0644");
    expect_true("current dir mode", mode_of(current.parent_path()) == 0755, "status profile dir should be 0755");
    fs::remove_all(root);
}

void test_write_history_entry() {
    fs::path root = test_root("history");
    fs::path history_root = root / "history";

    btrfsbackup::write_history_entry(durable_files(), history_root, sample_record());

    fs::path run_entry = history_root / "default" / "20260823T024407Z-4298-30158.json";
    fs::path last_entry = history_root / "default" / "last.json";
    expect_true("run history exists", fs::is_regular_file(run_entry), "missing run history");
    expect_true("last history exists", fs::is_regular_file(last_entry), "missing last history");
    expect_true("run mode", mode_of(run_entry) == 0600, "run history should be 0600");
    expect_true("last mode", mode_of(last_entry) == 0600, "last history should be 0600");
    expect_true("history root mode", mode_of(history_root) == 0700, "history root should be 0700");
    expect_true("history dir mode", mode_of(run_entry.parent_path()) == 0700, "history profile dir should be 0700");
    expect_eq("history content", btrfsbackup::dump_json(btrfsbackup::load_json_file(run_entry)), btrfsbackup::dump_json(btrfsbackup::load_json_file(last_entry)));
    fs::remove_all(root);
}

void test_rejects_unsafe_identifiers() {
    btrfsbackup::ProfileId profile_id("default");
    btrfsbackup::RunId run_id("20260823T024407Z-4298-30158");
    expect_eq("profile id wrapper", std::string(profile_id.value()), "default");
    expect_eq("run id wrapper", std::string(run_id.value()), "20260823T024407Z-4298-30158");

    expect_validation_error("bad profile", [] { (void)btrfsbackup::ProfileId{"../default"}; }, "invalid profile id");
    expect_validation_error("bad run", [] { (void)btrfsbackup::RunId{"../run"}; }, "invalid run id");
}

} // namespace

int main() {
    test_build_status_json_matches_contract_shape();
    test_build_status_json_includes_structured_error();
    test_build_public_status_json_excludes_diagnostics();
    test_dump_status_json_uses_stable_order_and_newline();
    test_write_current_status();
    test_write_history_entry();
    test_rejects_unsafe_identifiers();

    if (failures > 0) {
        return 1;
    }
    std::cout << "ok - status writer tests\n";
    return 0;
}
