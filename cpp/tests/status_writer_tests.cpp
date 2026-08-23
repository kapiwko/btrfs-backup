#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/identifiers.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/status_writer.hpp>

namespace fs = std::filesystem;

namespace {

using btrfsbackup::Json;
using btrfsbackup::StatusRecord;
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

StatusRecord sample_record() {
    return {
        .profile_id = "default",
        .profile_name = "Default backup",
        .run_id = "20260823T024407Z-4298-30158",
        .state = "succeeded",
        .phase = "succeeded",
        .message = "Backup completed.",
        .current_source_name = "Home",
        .source_index = 1,
        .source_count = 2,
        .started_at = "2026-08-23T02:44:07+00:00",
        .updated_at = "2026-08-23T02:45:07+00:00",
        .finished_at = "2026-08-23T02:45:07+00:00",
        .exit_code = 0,
    };
}

int mode_of(const fs::path& path) {
    struct stat info {};
    if (stat(path.c_str(), &info) != 0) {
        fail("stat", "cannot stat " + path.string());
        return 0;
    }
    return info.st_mode & 0777;
}

void test_build_status_json_matches_contract_shape() {
    Json data = btrfsbackup::build_status_json(sample_record());

    expect_true("schema", data.at("schemaVersion") == 1, "wrong schemaVersion");
    expect_true("profile id", data.at("profileId") == "default", "wrong profileId");
    expect_true("profile name", data.at("profileName") == "Default backup", "wrong profileName");
    expect_true("run id", data.at("runId") == "20260823T024407Z-4298-30158", "wrong runId");
    expect_true("state", data.at("state") == "succeeded", "wrong state");
    expect_true("phase", data.at("phase") == "succeeded", "wrong phase");
    expect_true("message", data.at("message") == "Backup completed.", "wrong message");
    expect_true("source", data.at("currentSourceName") == "Home", "wrong currentSourceName");
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
    expect_true("exit", data.at("exitCode") == 0, "wrong exitCode");
}

void test_build_status_json_includes_structured_error() {
    StatusRecord record = sample_record();
    record.state = "failed";
    record.phase = "validating-target";
    record.message = "Validation failed.";
    record.error_code = "target.btrfs_uuid_mismatch";
    record.error_message = "Target Btrfs UUID does not match.";
    record.details = {
        {"expected", "expected-uuid"},
        {"actual", "actual-uuid"},
    };
    record.recoverable = false;
    record.suggested_action = "connect-correct-target";
    record.exit_code = 2;

    Json data = btrfsbackup::build_status_json(record);

    expect_true("structured error code", data.at("errorCode") == "target.btrfs_uuid_mismatch", "wrong error code");
    expect_true("structured error message", data.at("errorMessage") == "Target Btrfs UUID does not match.", "wrong error message");
    expect_true("structured detail expected", data.at("details").at("expected") == "expected-uuid", "wrong expected detail");
    expect_true("structured detail actual", data.at("details").at("actual") == "actual-uuid", "wrong actual detail");
    expect_true("structured recoverable", data.at("recoverable") == false, "wrong recoverable");
    expect_true("structured action", data.at("suggestedAction") == "connect-correct-target", "wrong suggested action");
}

void test_dump_status_json_uses_stable_order_and_newline() {
    std::string dumped = btrfsbackup::dump_status_json(sample_record());

    expect_eq(
        "dump",
        dumped,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"profileId\": \"default\",\n"
        "  \"profileName\": \"Default backup\",\n"
        "  \"runId\": \"20260823T024407Z-4298-30158\",\n"
        "  \"state\": \"succeeded\",\n"
        "  \"phase\": \"succeeded\",\n"
        "  \"message\": \"Backup completed.\",\n"
        "  \"currentSourceName\": \"Home\",\n"
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
        "  \"exitCode\": 0\n"
        "}\n"
    );
}

void test_write_current_status() {
    fs::path root = test_root("current");
    fs::path status_root = root / "run" / "profiles";

    btrfsbackup::write_current_status(status_root, sample_record());

    fs::path current = status_root / "default" / "current.json";
    Json data = btrfsbackup::load_json_file(current);
    expect_true("current exists", fs::is_regular_file(current), "missing current.json");
    expect_true("current state", data.at("state") == "succeeded", "wrong current state");
    expect_true("current mode", mode_of(current) == 0644, "current.json should be 0644");
    expect_true("current dir mode", mode_of(current.parent_path()) == 0755, "status profile dir should be 0755");
    fs::remove_all(root);
}

void test_write_history_entry() {
    fs::path root = test_root("history");
    fs::path history_root = root / "history";

    btrfsbackup::write_history_entry(history_root, sample_record());

    fs::path run_entry = history_root / "default" / "20260823T024407Z-4298-30158.json";
    fs::path last_entry = history_root / "default" / "last.json";
    expect_true("run history exists", fs::is_regular_file(run_entry), "missing run history");
    expect_true("last history exists", fs::is_regular_file(last_entry), "missing last history");
    expect_true("run mode", mode_of(run_entry) == 0644, "run history should be 0644");
    expect_true("last mode", mode_of(last_entry) == 0644, "last history should be 0644");
    expect_true("history dir mode", mode_of(run_entry.parent_path()) == 0755, "history profile dir should be 0755");
    expect_eq("history content", btrfsbackup::dump_json(btrfsbackup::load_json_file(run_entry)), btrfsbackup::dump_json(btrfsbackup::load_json_file(last_entry)));
    fs::remove_all(root);
}

void test_rejects_unsafe_identifiers() {
    btrfsbackup::ProfileId profile_id("default");
    btrfsbackup::RunId run_id("20260823T024407Z-4298-30158");
    expect_eq("profile id wrapper", profile_id.value, "default");
    expect_eq("run id wrapper", run_id.value, "20260823T024407Z-4298-30158");

    StatusRecord bad_profile = sample_record();
    bad_profile.profile_id = "../default";
    expect_validation_error("bad profile", [&] { btrfsbackup::build_status_json(bad_profile); }, "invalid profile id");

    StatusRecord bad_run = sample_record();
    bad_run.run_id = "../run";
    expect_validation_error("bad run", [&] { btrfsbackup::build_status_json(bad_run); }, "invalid run id");
}

void test_invalid_profile_does_not_create_status_directory() {
    fs::path root = test_root("bad-write");
    StatusRecord bad_profile = sample_record();
    bad_profile.profile_id = "../default";

    expect_validation_error(
        "bad write",
        [&] { btrfsbackup::write_current_status(root / "status", bad_profile); },
        "invalid profile id"
    );
    expect_true("bad write side effect", !fs::exists(root / "default"), "invalid profile created escaped directory");
    expect_true("bad write status dir", !fs::exists(root / "status"), "invalid profile created status directory");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_build_status_json_matches_contract_shape();
    test_build_status_json_includes_structured_error();
    test_dump_status_json_uses_stable_order_and_newline();
    test_write_current_status();
    test_write_history_entry();
    test_rejects_unsafe_identifiers();
    test_invalid_profile_does_not_create_status_directory();

    if (failures > 0) {
        return 1;
    }
    std::cout << "ok - status writer tests\n";
    return 0;
}
