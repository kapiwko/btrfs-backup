// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <vector>

#include <config/json/Json.hpp>
#include <daemon/dbus/ManagerJsonCodec.hpp>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::config::json::Json;
using btrfsbackup::daemon::dbus::ManagerJsonCodec;

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
    const btrfsbackup::daemon::PublicStatusResponse status{
        .run = {
            .run_id = btrfsbackup::RunId{"20260829T160000Z-1-1"},
            .state = btrfsbackup::state::document::PublicRunState::Running,
            .phase = {.value = "sizing", .known = true},
            .activity = btrfsbackup::state::document::PublicActivity::Sizing,
            .can_cancel = true,
            .error_code = btrfsbackup::state::document::PublicErrorCode::None,
            .source_name = "Home",
            .target_name = "Backup disk",
            .progress = {
                .speed_bps = 10,
                .eta_seconds = 20,
                .source_percent = 30,
                .overall_percent = 40,
                .accuracy = btrfsbackup::state::ProgressAccuracy::Estimated,
            },
        },
        .last_success_at = "2026-08-25T10:00:00Z",
        .last_attempt_at = "2026-08-29T16:00:00Z",
        .last_attempt_state = "failed",
    };
    const Json status_document = Json::parse(codec.encode(status));
    expect_field("status", status_document, "schemaVersion", 4);
    expect_field("status", status_document, "runId", std::string(status.run.run_id->value()));
    expect_field("status", status_document, "activity", "sizing");
    expect_field("status", status_document, "canCancel", true);
    expect_field("status", status_document, "overallProgress", 40);
    expect_field("status", status_document, "lastSuccessAt", status.last_success_at);
    expect_field("status", status_document, "lastAttemptAt", status.last_attempt_at);
    expect_field("status", status_document, "lastAttemptState", status.last_attempt_state);

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
        .storage = btrfsbackup::state::document::TargetStorageStatusV1{
            .capacity_bytes = 1000,
            .used_bytes = 600,
            .available_bytes = 350,
            .usage_percent = 64,
            .measured_at = *btrfsbackup::parse_utc_timestamp("2026-08-30T12:34:56Z"),
            .live = true,
            .space_state = btrfsbackup::state::document::TargetSpaceState::BelowConfiguredMinimum,
        },
    };
    const Json target_document = Json::parse(codec.encode(target));
    expect_field("target", target_document, "safeToRemove", false);
    expect_field("target storage", target_document.at("storage"), "schemaVersion", 1);
    expect_field("target storage", target_document.at("storage"), "usedBytes", 600);
    expect_field("target storage", target_document.at("storage"), "live", true);
    test_helpers::expect_true("target privacy", !target_document.contains("device"), "private device field was encoded");
    test_helpers::expect_true(
        "target storage privacy",
        !target_document.at("storage").contains("luksUuid") &&
            !target_document.at("storage").contains("btrfsUuid") &&
            !target_document.at("storage").contains("partitionUuid"),
        "private target identity was encoded"
    );

    btrfsbackup::daemon::TargetStatus target_without_storage = target;
    target_without_storage.storage.reset();
    const Json target_without_storage_document = Json::parse(codec.encode(target_without_storage));
    test_helpers::expect_true(
        "target storage optional",
        !target_without_storage_document.contains("storage"),
        "missing measurement was encoded as storage data"
    );

    const btrfsbackup::daemon::OperationResult operation{
        .operation = "cancel-backup",
        .operation_id = "operation-1",
        .profile_id = "default",
        .run_id = "20260828T120000Z-1-1",
        .accepted = true,
    };
    const Json operation_document = Json::parse(codec.encode(operation));
    expect_field("operation", operation_document, "operation", "cancel-backup");
    expect_field("operation", operation_document, "operationId", operation.operation_id);
    expect_field("operation", operation_document, "runId", operation.run_id);
    expect_field("operation", operation_document, "accepted", true);
}

} // namespace

int main() {
    test_capabilities();
    test_profiles();
    test_status_history_and_device();
    return test_helpers::finish("manager JSON codec tests");
}
