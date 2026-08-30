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

void test_browse_session_requires_read_only_absolute_root() {
    const auto session = btrfsbackup::kde::parse_browse_session(QStringLiteral(R"({
        "schemaVersion": 1,
        "sessionId": "browse-1",
        "profileId": "default",
        "rootPath": "/run/btrfs-backup-browse/browse-1/repository",
        "expiresAt": "2026-08-31T12:00:00Z",
        "readOnly": true
    })"));
    expect(session.has_value() && session->session_id == QStringLiteral("browse-1"), "valid browse session was rejected");
    expect(!btrfsbackup::kde::parse_browse_session(QStringLiteral(R"({
        "schemaVersion": 1, "sessionId": "browse-1", "profileId": "default",
        "rootPath": "relative", "expiresAt": "2026-08-31T12:00:00Z", "readOnly": true
    })")).has_value(), "relative browse root was accepted");
    expect(!btrfsbackup::kde::parse_browse_session(QStringLiteral(R"({
        "schemaVersion": 1, "sessionId": "browse-1", "profileId": "default",
        "rootPath": "/tmp/root", "expiresAt": "2026-08-31T12:00:00Z", "readOnly": false
    })")).has_value(), "writable browse session was accepted");
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
    test_browse_session_requires_read_only_absolute_root();
    test_backup_coverage_is_sanitized();
    if (failures == 0) {
        std::cout << "manager API tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
