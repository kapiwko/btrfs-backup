// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TargetStatusModel.hpp"

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

void test_applies_lifecycle_and_storage() {
    TargetStatusModel model;
    model.setStorageSupported(true);
    const bool applied = model.apply("default", payload(QStringLiteral(R"(,
        "storage": {
            "schemaVersion": 1,
            "capacityBytes": 1000,
            "usedBytes": 400,
            "availableBytes": 600,
            "usagePercent": 40,
            "measuredAt": "2026-08-30T12:34:56Z",
            "live": false,
            "spaceState": "below-configured-minimum"
        })")));

    expect(applied, "valid target status was rejected");
    expect(model.connected() && model.mounted(), "target lifecycle was not applied");
    expect(model.storageKnown(), "target storage was not applied");
    expect(model.capacityBytes() == 1000 && model.usagePercent() == 40, "target capacity is wrong");
    expect(model.spaceBelowMinimum(), "minimum-space warning was lost");
    expect(!model.storageLive(), "cached storage was marked live");
}

void test_capability_and_reset_clear_storage() {
    TargetStatusModel model;
    expect(model.apply("default", payload()), "base target status was rejected");
    expect(!model.storageKnown(), "missing storage was treated as known");

    model.setStorageSupported(true);
    expect(model.storageSupported(), "storage capability was not enabled");
    model.setStorageSupported(false);
    expect(!model.storageSupported() && !model.storageKnown(), "disabled storage capability retained data");

    model.reset();
    expect(model.state() == QStringLiteral("unknown") && !model.connected(), "reset retained lifecycle state");
}

void test_wrong_profile_preserves_last_state() {
    TargetStatusModel model;
    expect(model.apply("default", payload()), "initial target status was rejected");
    expect(!model.apply("other", payload()), "status for another profile was accepted");
    expect(model.state() == QStringLiteral("mounted"), "rejected status changed the model");
}

} // namespace

int main() {
    test_applies_lifecycle_and_storage();
    test_capability_and_reset_clear_storage();
    test_wrong_profile_preserves_last_state();
    if (failures == 0) {
        std::cout << "target status model tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
