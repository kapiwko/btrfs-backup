// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <vector>

#include <config/model/json.hpp>
#include <daemon/manager_json_codec.hpp>

#include "support/test_helpers.hpp"

namespace {

using btrfsbackup::config::Json;
using btrfsbackup::daemon::ManagerJsonCodec;

void expect_field(const std::string& name, const Json& document, const std::string& field, const Json& expected) {
    test_helpers::expect_true(name + " field", document.contains(field), "missing field " + field);
    if (document.contains(field))
        test_helpers::expect_true(name + " value", document.at(field) == expected, "unexpected value for " + field);
}

void test_capabilities() {
    const ManagerJsonCodec codec;
    const btrfsbackup::daemon::ManagerCapabilities capabilities{
        .interface_name = "io.github.btrfsbackup.Manager1",
        .features = {"profiles", "status"},
    };
    const Json document = Json::parse(codec.encode(capabilities));
    expect_field("capabilities", document, "schemaVersion", 1);
    expect_field("capabilities", document, "interface", capabilities.interface_name);
    expect_field("capabilities", document, "readOnly", true);
    expect_field("capabilities", document, "features", capabilities.features);
}

void test_profiles() {
    const ManagerJsonCodec codec;
    const std::vector<btrfsbackup::daemon::ProfileSummary> profiles{{
        .profile_id = "default",
        .name = "Default backup",
        .target_name = "Backup disk",
        .sources = {{.id = "home", .name = "Home"}},
    }};
    const Json document = Json::parse(codec.encode(profiles));
    test_helpers::expect_true("profiles array", document.is_array() && document.size() == 1, "invalid profile list");
    expect_field("profile", document.at(0), "profileId", "default");
    expect_field("profile source", document.at(0).at("sources").at(0), "name", "Home");
    test_helpers::expect_true("profile privacy", !document.at(0).contains("device"), "private device field was encoded");
}

void test_status_history_and_device() {
    const ManagerJsonCodec codec;
    const btrfsbackup::daemon::PublicRunStatus status{
        .state = "running",
        .error_code = "",
        .source_name = "Home",
        .target_name = "Backup disk",
        .speed_bps = 10,
        .eta_seconds = 20,
        .source_progress = 30,
        .overall_progress = 40,
        .progress_accuracy = "estimated",
    };
    const Json status_document = Json::parse(codec.encode(status));
    expect_field("status", status_document, "schemaVersion", 3);
    expect_field("status", status_document, "overallProgress", 40);

    const btrfsbackup::daemon::SanitizedHistoryPage history{{{
        .state = "failed",
        .error_code = "backup.failed",
        .source_name = "Home",
        .target_name = "Backup disk",
        .finished_at = "2026-08-25T10:00:00Z",
        .overall_progress = 40,
    }}};
    const Json history_document = Json::parse(codec.encode(history));
    expect_field("history", history_document.at(0), "errorCode", "backup.failed");
    test_helpers::expect_true(
        "history privacy",
        !history_document.at(0).contains("details") && !history_document.at(0).contains("runId"),
        "private history fields were encoded"
    );

    const btrfsbackup::daemon::TargetStatus target{
        .profile_id = "default",
        .target_name = "Backup disk",
        .state = "mounted",
        .connected = true,
        .unlocked = true,
        .mounted = true,
        .safe_to_remove = false,
    };
    const Json target_document = Json::parse(codec.encode(target));
    expect_field("target", target_document, "safeToRemove", false);
    test_helpers::expect_true("target privacy", !target_document.contains("device"), "private device field was encoded");
}

} // namespace

int main() {
    test_capabilities();
    test_profiles();
    test_status_history_and_device();
    return test_helpers::finish("manager JSON codec tests");
}
