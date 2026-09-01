// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <filesystem>
#include <string>

#include <config/json/JsonIo.hpp>
#include <daemon/query/HistoryQueryService.hpp>
#include <daemon/ManagerService.hpp>
#include <daemon/query/ProfileQueryService.hpp>
#include <daemon/query/StatusQueryService.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::daemon::ManagerPaths manager_paths(const fs::path& root) {
    return {
        .config_root = root / "etc",
        .public_profile_root = root / "public",
        .status_root = root / "status",
        .history_root = root / "history",
        .state_root = root / "state",
        .target_mount_root = root / "mnt",
        .mapper_root = root / "mapper",
        .mountinfo_path = "/proc/self/mountinfo",
    };
}

std::string public_status() {
    return R"({
  "schemaVersion": 3,
  "runId": "20260829T160000Z-1-1",
  "state": "running",
  "phase": "sizing",
  "activity": "sizing",
  "canCancel": true,
  "errorCode": "",
  "sourceName": "Home",
  "targetName": "Backup disk",
  "speedBps": 10,
  "etaSeconds": 20,
  "sourceProgress": 30,
  "overallProgress": 40,
  "progressAccuracy": "estimated",
  "sourceIndex": 1,
  "sourceCount": 2,
  "startedAt": "2026-08-29T15:00:00Z",
  "updatedAt": "2026-08-29T16:00:00Z",
  "privateField": "must not cross the boundary"
})";
}

std::string private_history(
    const std::string& run_id,
    const std::string& state,
    const std::string& finished_at
) {
    return btrfsbackup::config::json::Json({
                                               {"schemaVersion", 2},
                                               {"profileId", "default"},
                                               {"profileName", "Default backup"},
                                               {"runId", run_id},
                                               {"state", state},
                                               {"phase", state},
                                               {"message", "private message"},
                                               {"currentSourceName", "Home"},
                                               {"targetName", "Backup disk"},
                                               {"sourceIndex", 1},
                                               {"sourceCount", 1},
                                               {"startedAt", "2026-08-25T09:00:00Z"},
                                               {"updatedAt", finished_at},
                                               {"finishedAt", finished_at},
                                               {"errorCode", state == "succeeded" ? "" : "repository.private_failure"},
                                               {"errorMessage", state == "succeeded" ? "" : "private failure"},
                                               {"details", {{"device", "/dev/private"}}},
                                               {"runBytesProcessed", 4294967296ULL},
                                               {"recoverable", false},
                                               {"suggestedAction", ""},
                                               {"canCancel", false},
                                               {"bytesProcessed", 100},
                                               {"bytesTotalEstimated", 100},
                                               {"runBytesProcessed", 100},
                                               {"speedBps", 0},
                                               {"etaSeconds", -1},
                                               {"sourceProgress", 100},
                                               {"overallProgress", 100},
                                               {"progressAccuracy", "exact"},
                                               {"exitCode", state == "succeeded" ? 0 : 1},
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

    btrfsbackup::daemon::ManagerService service(manager_paths(root));
    const btrfsbackup::daemon::ManagerCapabilities capabilities = service.get_capabilities();
    test_helpers::expect_true("operational capability", !capabilities.read_only, "manager is still read-only");
    test_helpers::expect_true("manager API major", capabilities.api_major == 2, "manager API major was not advanced");
    test_helpers::expect_true("manager API minor", capabilities.api_minor == 4, "manager API minor was not updated");
    test_helpers::expect_true(
        "profile administration capability",
        std::ranges::find(capabilities.features, "profile-administration") != capabilities.features.end(),
        "manager omits profile administration capability"
    );
    test_helpers::expect_true(
        "manager status schema",
        capabilities.public_status_schema_version == 5,
        "manager did not advertise the backup summary schema"
    );
    test_helpers::expect_true(
        "target storage capability",
        std::ranges::find(capabilities.features, "target-storage-usage") != capabilities.features.end(),
        "manager omits target storage capability"
    );
    test_helpers::expect_true(
        "change signal capability",
        std::ranges::find(capabilities.features, "change-signals") != capabilities.features.end(),
        "manager omits the change signal capability"
    );
    test_helpers::expect_true(
        "target credentials capability",
        std::ranges::find(capabilities.features, "target-credentials") != capabilities.features.end(),
        "manager omits target credential management"
    );
    test_helpers::expect_true(
        "device provisioning capability",
        std::ranges::find(capabilities.features, "device-provisioning") != capabilities.features.end(),
        "manager omits device provisioning"
    );
    test_helpers::expect_true(
        "sanitized history schema capability",
        capabilities.history_schema_version == 3,
        "manager advertises the private history schema"
    );
    test_helpers::expect_true(
        "device state schema capability",
        capabilities.device_state_schema_version == 1,
        "manager omits the device-state schema"
    );
    const btrfsbackup::daemon::query::ProfileQueryService profiles_service(root / "public");
    const std::vector<btrfsbackup::daemon::ProfileSummary> profiles = profiles_service.list_profiles();
    test_helpers::expect_eq("one public profile", std::to_string(profiles.size()), "1");
    test_helpers::expect_eq("profile id", profiles.at(0).profile_id, "default");
    test_helpers::expect_true("profile enabled default", profiles.at(0).enabled, "missing enabled default changed behavior");
    test_helpers::expect_eq("profile source id", profiles.at(0).sources.at(0).id, "home");
    fs::remove_all(root);
}

void test_status_and_history_sanitization() {
    fs::path root = test_helpers::test_root("manager-service", "status-history");
    test_helpers::write_file(root / "status" / "default" / "current.json", public_status());
    test_helpers::write_file(
        root / "history" / "default" / "20260825T100000Z-1-1.json",
        private_history("20260825T100000Z-1-1", "succeeded", "2026-08-25T10:00:00Z")
    );
    test_helpers::write_file(
        root / "history" / "default" / "20260825T110000Z-1-2.json",
        private_history("20260825T110000Z-1-2", "failed", "2026-08-25T11:00:00Z")
    );
    test_helpers::write_file(
        root / "history" / "default" / "20260825T120000Z-1-3.json",
        private_history("20260825T120000Z-1-3", "failed", "2026-08-25T12:00:00Z")
    );
    test_helpers::write_file(
        root / "history" / "default" / "20260825T130000Z-1-4.json",
        private_history("20260825T130000Z-1-4", "failed", "2026-08-25T13:00:00Z")
    );
    test_helpers::write_file(
        root / "history" / "default" / "last.json",
        private_history("20260825T130000Z-1-4", "failed", "2026-08-25T13:00:00Z")
    );
    test_helpers::write_file(
        root / "state" / "profiles" / "default" / "last-success",
        "date=2026-08-25\ntimestamp=2026-08-25T10:00:00+0000\n"
    );

    const btrfsbackup::daemon::query::HistoryQueryService history_service(root / "history");
    const btrfsbackup::daemon::query::StatusQueryService status_service(
        root / "status",
        root / "state",
        history_service
    );
    const btrfsbackup::daemon::PublicStatusResponse status = status_service.get_status("default");
    test_helpers::expect_eq("status state", btrfsbackup::state::document::public_run_state_name(status.run), "running");
    test_helpers::expect_eq("status phase", status.run.phase.value, "sizing");
    test_helpers::expect_eq("status activity", btrfsbackup::state::document::public_activity_name(status.run), "sizing");
    test_helpers::expect_true("status cancellable", status.run.can_cancel, "status lost cancellation capability");
    test_helpers::expect_eq("status source", status.run.source_name, "Home");
    test_helpers::expect_true(
        "status source position",
        status.source_index == 1 && status.source_count == 2,
        "status lost source position"
    );
    test_helpers::expect_eq("status started time", status.started_at, "2026-08-29T15:00:00Z");
    test_helpers::expect_eq("last success is independent from history page", status.last_success_at, "2026-08-25T10:00:00+0000");
    test_helpers::expect_eq("last attempt timestamp", status.last_attempt_at, "2026-08-25T13:00:00Z");
    test_helpers::expect_eq("last attempt state", status.last_attempt_state, "failed");
    const btrfsbackup::daemon::SanitizedHistoryPage history = history_service.get_history_sanitized("default", 0, 1);
    test_helpers::expect_eq("bounded history", std::to_string(history.entries.size()), "1");
    test_helpers::expect_eq("newest history first", history.entries.at(0).state, "failed");
    test_helpers::expect_eq("generalized history error", history.entries.at(0).error_code, "backup.failed");
    test_helpers::expect_eq("history started time", history.entries.at(0).started_at, "2026-08-25T09:00:00Z");
    test_helpers::expect_true("history source count", history.entries.at(0).source_count == 1, "history lost source count");
    test_helpers::expect_true(
        "history transferred bytes",
        history.entries.at(0).bytes_transferred == 4294967296ULL,
        "history lost transferred byte count"
    );
    test_helpers::expect_validation_error(
        "history limit",
        [&] { (void)history_service.get_history_sanitized("default", 0, 101); },
        "between 1 and 100"
    );
    fs::remove(root / "status" / "default" / "current.json");
    test_helpers::expect_eq(
        "restart fallback state",
        btrfsbackup::state::document::public_run_state_name(status_service.get_status("default").run),
        "failed"
    );
    fs::remove_all(root);
}

void test_last_history_cache_recovers_from_authoritative_record() {
    fs::path root = test_helpers::test_root("manager-service", "last-cache");
    const fs::path history = root / "history" / "default";
    test_helpers::write_file(
        history / "20260825T120000Z-1-2.json",
        private_history("20260825T120000Z-1-2", "succeeded", "2026-08-25T12:00:00Z")
    );
    test_helpers::write_file(
        history / "last.json",
        private_history("20260825T110000Z-1-1", "failed", "2026-08-25T11:00:00Z")
    );
    const btrfsbackup::daemon::query::HistoryQueryService service(root / "history");

    test_helpers::expect_eq(
        "stale last cache",
        service.get_last_sanitized("default")->state,
        "succeeded"
    );
    fs::remove(history / "last.json");
    test_helpers::expect_eq(
        "missing last cache",
        service.get_last_sanitized("default")->state,
        "succeeded"
    );
    test_helpers::write_file(history / "last.json", "{invalid");
    test_helpers::expect_eq(
        "malformed last cache",
        service.get_last_sanitized("default")->state,
        "succeeded"
    );
    fs::remove_all(root);
}

void test_malformed_and_oversized_documents() {
    fs::path root = test_helpers::test_root("manager-service", "invalid-documents");
    const btrfsbackup::daemon::query::HistoryQueryService history_service(root / "history");
    const btrfsbackup::daemon::query::StatusQueryService status_service(
        root / "status",
        root / "state",
        history_service
    );
    test_helpers::write_file(root / "status" / "default" / "current.json", "{invalid");
    test_helpers::expect_validation_error(
        "malformed status",
        [&] { (void)status_service.get_status("default"); },
        "invalid public status JSON"
    );
    test_helpers::write_file(
        root / "status" / "default" / "current.json",
        std::string(1024 * 1024 + 1, 'x')
    );
    test_helpers::expect_validation_error(
        "oversized status",
        [&] { (void)status_service.get_status("default"); },
        "exceeds the size limit"
    );
    fs::remove_all(root);
}

} // namespace

int main() {
    test_capabilities_and_profiles();
    test_status_and_history_sanitization();
    test_last_history_cache_recovers_from_authoritative_record();
    test_malformed_and_oversized_documents();
    return test_helpers::finish("manager service tests");
}
