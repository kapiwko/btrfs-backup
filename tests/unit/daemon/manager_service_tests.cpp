// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <string>

#include <config/json_io.hpp>
#include <config/profile.hpp>
#include <daemon/manager_service.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::ManagerPaths manager_paths(const fs::path& root) {
    return {
        .config_root = root / "etc",
        .public_profile_root = root / "public",
        .status_root = root / "status",
        .history_root = root / "history",
        .target_mount_root = root / "mnt",
        .mapper_root = root / "mapper",
        .mountinfo_path = "/proc/self/mountinfo",
    };
}

btrfsbackup::Json private_profile(const fs::path& root) {
    return {
        {"schemaVersion", 3},
        {"profileId", "default"},
        {"name", "Default backup"},
        {"enabled", true},
        {"target", {
                       {"device", "/dev/null"},
                       {"luksUuid", "11111111-2222-3333-4444-555555555555"},
                       {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"},
                       {"mapperName", "backupdisk"},
                   }},
        {"sources", btrfsbackup::Json::array({{
                        {"id", "home"},
                        {"name", "Home"},
                        {"enabled", true},
                        {"subvolume", "/home"},
                        {"localSnapshotDir", "/.snapshots/home"},
                        {"remoteSubdir", "home"},
                        {"remoteRetention", 2},
                        {"localRetention", 2},
                    }})},
    };
}

std::string public_status() {
    return R"({
  "schemaVersion": 3,
  "state": "running",
  "errorCode": "",
  "sourceName": "Home",
  "targetName": "Backup disk",
  "speedBps": 10,
  "etaSeconds": 20,
  "sourceProgress": 30,
  "overallProgress": 40,
  "progressAccuracy": "estimated",
  "privateField": "must not cross the boundary"
})";
}

std::string private_history(const std::string& state, const std::string& finished_at) {
    return btrfsbackup::Json({
                                 {"schemaVersion", 2},
                                 {"state", state},
                                 {"errorCode", state == "succeeded" ? "" : "repository.private_failure"},
                                 {"currentSourceName", "Home"},
                                 {"targetName", "Backup disk"},
                                 {"finishedAt", finished_at},
                                 {"overallProgress", 100},
                                 {"details", {{"device", "/dev/private"}}},
                                 {"runId", "private-run-id"},
                             })
        .dump();
}

void test_capabilities_and_profiles() {
    fs::path root = test_helpers::test_root("manager-service", "profiles");
    test_helpers::write_file(
        root / "public" / "default.json",
        R"({"schemaVersion":1,"profileId":"default","name":"Default backup","target":{"name":"Backup disk"},"sources":[{"id":"home","name":"Home","path":"/private"}],"private":"hidden"})"
    );
    fs::create_symlink(root / "public" / "default.json", root / "public" / "linked.json");

    btrfsbackup::ManagerService service(manager_paths(root));
    const btrfsbackup::Json capabilities = service.get_capabilities();
    test_helpers::expect_true("read-only capability", capabilities.at("readOnly") == true, "manager is not read-only");
    test_helpers::expect_true(
        "sanitized history schema capability",
        capabilities.at("historySchemaVersion") == 1,
        "manager advertises the private history schema"
    );
    test_helpers::expect_true(
        "device state schema capability",
        capabilities.at("deviceStateSchemaVersion") == 1,
        "manager omits the device-state schema"
    );
    const btrfsbackup::Json profiles = service.list_profiles();
    test_helpers::expect_eq("one public profile", std::to_string(profiles.size()), "1");
    test_helpers::expect_true("private profile field absent", !profiles.at(0).contains("private"), "private field leaked");
    test_helpers::expect_true(
        "private source path absent",
        !profiles.at(0).at("sources").at(0).contains("path"),
        "source path leaked"
    );
    fs::remove_all(root);
}

void test_status_and_history_sanitization() {
    fs::path root = test_helpers::test_root("manager-service", "status-history");
    test_helpers::write_file(root / "status" / "default" / "current.json", public_status());
    test_helpers::write_file(
        root / "history" / "default" / "20260825T100000Z-1-1.json",
        private_history("succeeded", "2026-08-25T10:00:00Z")
    );
    test_helpers::write_file(
        root / "history" / "default" / "20260825T110000Z-1-2.json",
        private_history("failed", "2026-08-25T11:00:00Z")
    );
    test_helpers::write_file(
        root / "history" / "default" / "last.json",
        private_history("failed", "2026-08-25T11:00:00Z")
    );

    btrfsbackup::ManagerService service(manager_paths(root));
    const btrfsbackup::Json status = service.get_status("default");
    test_helpers::expect_true("status private field absent", !status.contains("privateField"), "status field leaked");
    const btrfsbackup::Json history = service.get_history_sanitized("default", 0, 1);
    test_helpers::expect_eq("bounded history", std::to_string(history.size()), "1");
    test_helpers::expect_eq("newest history first", history.at(0).at("state"), "failed");
    test_helpers::expect_eq("generalized history error", history.at(0).at("errorCode"), "backup.failed");
    test_helpers::expect_true("history details absent", !history.at(0).contains("details"), "details leaked");
    test_helpers::expect_true("history run id absent", !history.at(0).contains("runId"), "run id leaked");
    test_helpers::expect_validation_error(
        "history limit",
        [&] { (void)service.get_history_sanitized("default", 0, 101); },
        "between 1 and 100"
    );
    fs::remove(root / "status" / "default" / "current.json");
    test_helpers::expect_eq("restart fallback state", service.get_status("default").at("state"), "failed");
    fs::remove_all(root);
}

void test_malformed_and_oversized_documents() {
    fs::path root = test_helpers::test_root("manager-service", "invalid-documents");
    btrfsbackup::ManagerService service(manager_paths(root));
    test_helpers::write_file(root / "status" / "default" / "current.json", "{invalid");
    test_helpers::expect_validation_error(
        "malformed status",
        [&] { (void)service.get_status("default"); },
        "invalid manager JSON"
    );
    test_helpers::write_file(
        root / "status" / "default" / "current.json",
        std::string(1024 * 1024 + 1, 'x')
    );
    test_helpers::expect_validation_error(
        "oversized status",
        [&] { (void)service.get_status("default"); },
        "exceeds the size limit"
    );
    fs::remove_all(root);
}

void test_device_state_is_presentation_safe() {
    fs::path root = test_helpers::test_root("manager-service", "device");
    test_helpers::write_file(
        root / "etc" / "profiles" / "default" / "profile.json",
        btrfsbackup::dump_json(private_profile(root))
    );
    btrfsbackup::ManagerService service(manager_paths(root));
    const btrfsbackup::Json state = service.get_device_state("default");
    test_helpers::expect_eq("connected target state", state.at("state"), "connected");
    test_helpers::expect_true("safe closed target", state.at("safeToRemove") == true, "target is not safe");
    test_helpers::expect_true("device path absent", !state.contains("device"), "device path leaked");
    test_helpers::expect_true("UUID absent", !state.contains("luksUuid"), "UUID leaked");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_capabilities_and_profiles();
    test_status_and_history_sanitization();
    test_malformed_and_oversized_documents();
    test_device_state_is_presentation_safe();
    return test_helpers::finish("manager service tests");
}
