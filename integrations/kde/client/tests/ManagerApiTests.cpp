// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ManagerApi.hpp"

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

QString payload(QString storage = {}) {
    return QStringLiteral(R"({
        "schemaVersion": 1,
        "profileId": "default",
        "targetName": "backupdisk",
        "state": "mounted",
        "connected": true,
        "unlocked": true,
        "mounted": true,
        "safeToRemove": false%1
    })")
        .arg(storage);
}

void test_complete_and_missing_storage() {
    const auto complete = btrfsbackup::kde::parse_target_status(payload(QStringLiteral(R"(,
        "storage": {
            "schemaVersion": 1,
            "capacityBytes": 4000787030016,
            "usedBytes": 1280251849600,
            "availableBytes": 2720535180416,
            "usagePercent": 32,
            "measuredAt": "2026-08-30T12:34:56Z",
            "live": true,
            "spaceState": "normal"
        })")));
    expect(complete.has_value(), "complete target status was rejected");
    expect(complete.has_value() && complete->storage.has_value(), "storage block was not decoded");
    expect(complete.has_value() && complete->storage->usage_percent == 32, "usage percent is wrong");

    const auto missing = btrfsbackup::kde::parse_target_status(payload());
    expect(missing.has_value(), "target status without storage was rejected");
    expect(missing.has_value() && !missing->storage.has_value(), "missing storage was treated as known");
}

void test_invalid_storage_preserves_target_state() {
    const auto invalid = btrfsbackup::kde::parse_target_status(payload(QStringLiteral(R"(,
        "storage": {
            "schemaVersion": 1,
            "capacityBytes": 100,
            "usedBytes": 101,
            "availableBytes": 10,
            "usagePercent": 101,
            "measuredAt": "not-a-time",
            "live": true,
            "spaceState": "unknown"
        })")));
    expect(invalid.has_value(), "invalid optional storage rejected target lifecycle state");
    expect(invalid.has_value() && !invalid->storage.has_value(), "invalid storage block was accepted");
    expect(invalid.has_value() && invalid->mounted, "target lifecycle fields were lost");
}

void test_invalid_parent_is_rejected() {
    expect(
        !btrfsbackup::kde::parse_target_status(QStringLiteral(R"({"schemaVersion":2})")).has_value(),
        "unsupported parent schema was accepted"
    );
}

void test_profile_configuration_health_is_decoded() {
    const auto profiles = btrfsbackup::kde::parse_profiles(QStringLiteral(R"([{
        "schemaVersion":2,"profileId":"default","name":"Default","enabled":true,
        "targetName":"Backup disk","sources":[],"configurationValid":false,
        "configurationErrorCode":"configuration.source_missing"
    }])"));
    expect(profiles.has_value() && profiles->size() == 1, "profile summary was rejected");
    expect(profiles.has_value() && !profiles->front().configuration_valid, "configuration health was ignored");
    expect(
        profiles.has_value() && profiles->front().configuration_error_code == QStringLiteral("configuration.source_missing"),
        "configuration health code was ignored"
    );
}

void test_run_transfer_bytes_are_decoded() {
    const auto run = btrfsbackup::kde::parse_status(QStringLiteral(R"({
        "schemaVersion":5,"runId":"run-1","state":"running","phase":"transferring",
        "activity":"transferring","canCancel":true,"errorCode":"","sourceName":"Home",
        "targetName":"Backup disk","bytesProcessed":1048576,"bytesTotalEstimated":4194304,
        "speedBps":1024,"etaSeconds":20,"sourceProgress":25,"overallProgress":25,
        "progressAccuracy":"exact","sourceIndex":1,"sourceCount":1,
        "startedAt":"2026-09-03T12:00:00Z","updatedAt":"2026-09-03T12:00:01Z",
        "lastSuccessAt":"","lastAttemptAt":"","lastAttemptState":""
    })"));
    expect(run.has_value(), "run status with transfer byte counts was rejected");
    expect(run.has_value() && run->bytes_processed == 1048576, "processed byte count was not decoded");
    expect(run.has_value() && run->bytes_total_estimated == 4194304, "estimated byte count was not decoded");
}

void test_history_is_decoded_once_for_every_kde_client() {
    const auto history = btrfsbackup::kde::parse_history(QStringLiteral(R"([{
        "schemaVersion":3,"state":"succeeded","errorCode":"","sourceName":"Home",
        "targetName":"Backup disk","startedAt":"2026-09-03T12:00:00Z",
        "finishedAt":"2026-09-03T12:01:30Z","sourceCount":2,"overallProgress":100,
        "bytesTransferred":1048576
    }])"));
    expect(history.has_value() && history->size() == 1, "valid history was rejected");
    expect(history.has_value() && history->front().duration_seconds == 90, "history duration is wrong");
    expect(history.has_value() && history->front().bytes_transferred == 1048576, "history byte count is wrong");
    expect(!btrfsbackup::kde::parse_history(QStringLiteral(R"([{
        "schemaVersion":3,"startedAt":"invalid","finishedAt":"2026-09-03T12:01:30Z"
    }])")).has_value(), "invalid history timestamp was accepted");
}

void test_browse_session_requires_read_only_absolute_root() {
    const auto session = btrfsbackup::kde::parse_browse_session(QStringLiteral(R"({
        "schemaVersion": 2,
        "sessionId": "browse-1",
        "profileId": "default",
        "expiresAt": "2026-08-31T12:00:00Z",
        "readOnly": true
    })"));
    expect(session.has_value() && session->session_id == QStringLiteral("browse-1"), "valid browse session was rejected");
    expect(!btrfsbackup::kde::parse_browse_session(QStringLiteral(R"({
        "schemaVersion": 1, "sessionId": "browse-1", "profileId": "default",
        "expiresAt": "2026-08-31T12:00:00Z", "readOnly": true
    })"))
                .has_value(),
           "legacy browse session was accepted");
    expect(!btrfsbackup::kde::parse_browse_session(QStringLiteral(R"({
        "schemaVersion": 2, "sessionId": "browse-1", "profileId": "default",
        "expiresAt": "2026-08-31T12:00:00Z", "readOnly": false
    })"))
                .has_value(),
           "writable browse session was accepted");
}

void test_backup_coverage_is_sanitized() {
    const auto coverage = btrfsbackup::kde::parse_backup_coverage(QStringLiteral(R"([
        {"profileId":"default","sourceId":"home","relativePath":"Documents/report.txt"}
    ])"));
    expect(coverage.has_value() && coverage->size() == 1, "valid backup coverage was rejected");
    expect(!btrfsbackup::kde::parse_backup_coverage(QStringLiteral(R"([
        {"profileId":"default","sourceId":"home","relativePath":"../private"}
    ])")).has_value(), "traversal coverage was accepted");
}

} // namespace

int main() {
    test_complete_and_missing_storage();
    test_invalid_storage_preserves_target_state();
    test_invalid_parent_is_rejected();
    test_profile_configuration_health_is_decoded();
    test_run_transfer_bytes_are_decoded();
    test_history_is_decoded_once_for_every_kde_client();
    test_browse_session_requires_read_only_absolute_root();
    test_backup_coverage_is_sanitized();
    if (failures == 0) {
        std::cout << "manager API tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
