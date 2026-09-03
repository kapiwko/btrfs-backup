// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <utility>
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
        .enabled = false,
        .target_name = "Backup disk",
        .sources = {{.id = "home", .name = "Home"}},
        .configuration_valid = false,
        .configuration_error_code = "configuration.source_missing",
    }};
    const Json document = Json::parse(codec.encode(profiles));
    test_helpers::expect_true("profiles array", document.is_array() && document.size() == 1, "invalid profile list");
    expect_field("profile", document.at(0), "profileId", "default");
    expect_field("profile", document.at(0), "enabled", false);
    expect_field("profile", document.at(0), "configurationValid", false);
    expect_field("profile", document.at(0), "configurationErrorCode", "configuration.source_missing");
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
                .bytes_processed = 1048576,
                .bytes_total_estimated = 4194304,
                .speed_bps = 10,
                .eta_seconds = 20,
                .source_percent = 30,
                .overall_percent = 40,
                .accuracy = btrfsbackup::state::ProgressAccuracy::Estimated,
            },
        },
        .source_index = 1,
        .source_count = 2,
        .started_at = "2026-08-29T15:00:00Z",
        .updated_at = "2026-08-29T16:00:00Z",
        .last_success_at = "2026-08-25T10:00:00Z",
        .last_attempt_at = "2026-08-29T16:00:00Z",
        .last_attempt_state = "failed",
    };
    const Json status_document = Json::parse(codec.encode(status));
    expect_field("status", status_document, "schemaVersion", 5);
    expect_field("status", status_document, "runId", std::string(status.run.run_id->value()));
    expect_field("status", status_document, "activity", "sizing");
    expect_field("status", status_document, "canCancel", true);
    expect_field("status", status_document, "overallProgress", 40);
    expect_field("status", status_document, "bytesProcessed", 1048576);
    expect_field("status", status_document, "bytesTotalEstimated", 4194304);
    expect_field("status", status_document, "sourceIndex", 1);
    expect_field("status", status_document, "sourceCount", 2);
    expect_field("status", status_document, "lastSuccessAt", status.last_success_at);
    expect_field("status", status_document, "lastAttemptAt", status.last_attempt_at);
    expect_field("status", status_document, "lastAttemptState", status.last_attempt_state);

    const btrfsbackup::daemon::SanitizedHistoryPage history{{{
        .state = "failed",
        .error_code = "backup.failed",
        .source_name = "Home",
        .target_name = "Backup disk",
        .started_at = "2026-08-25T09:00:00Z",
        .finished_at = "2026-08-25T10:00:00Z",
        .source_count = 2,
        .overall_progress = 40,
        .bytes_transferred = 4294967296,
    }}};
    const Json history_document = Json::parse(codec.encode(history));
    expect_field("history", history_document.at(0), "errorCode", "backup.failed");
    expect_field("history", history_document.at(0), "bytesTransferred", 4294967296);
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

    const Json details = Json::parse(codec.encode(btrfsbackup::daemon::control::ProfileDetails{"default", "generation", "fingerprint", R"({"profileId":"default","target":{"activation":{"mode":"keyFile","keyFile":"/root/key"}},"hooks":{"beforeSnapshot":["secret-command"]}})", false, "configuration.source_not_subvolume", {"/home", "/srv/work"}}));
    test_helpers::expect_true(
        "details hooks privacy",
        !details.at("document").contains("hooks"),
        "profile details exposed hooks"
    );
    test_helpers::expect_true(
        "details key privacy",
        !details.at("document").at("target").at("activation").contains("keyFile"),
        "profile details exposed the key path"
    );
    expect_field("details activation", details.at("document").at("target").at("activation"), "mode", "keyFile");
    expect_field("details health", details, "configurationValid", false);
    expect_field("details health code", details, "configurationErrorCode", "configuration.source_not_subvolume");
    expect_field("details candidates", details, "sourceCandidates", std::vector<std::string>{"/home", "/srv/work"});
}

void test_storage_topology_and_plan_contract() {
    namespace provisioning = btrfsbackup::daemon::provisioning;
    const ManagerJsonCodec codec;
    provisioning::StorageDevice device;
    device.candidate_id = "opaque-device";
    device.identity.display_path = "/dev/sdb";
    device.identity.major_minor = "8:16";
    device.display_name = "Backup disk";
    device.hotplug = true;
    device.system_device = true;
    device.size_bytes = 1024;
    device.logical_sector_size = 512;
    device.partition_table.type = provisioning::PartitionTableType::Gpt;
    provisioning::ExistingPartition partition;
    partition.candidate_id = "opaque-partition";
    partition.identity.display_path = "/dev/sdb1";
    partition.identity.major_minor = "8:17";
    partition.partition_number = 1;
    partition.start_sector = 1;
    partition.sector_count = 1;
    partition.filesystem.type = "ext4";
    device.regions.emplace_back(std::move(partition));
    device.regions.emplace_back(provisioning::UnallocatedRegion{
        .id = "opaque-free",
        .start_sector = 2,
        .sector_count = 1,
        .suitable_for_backup_partition = true,
    });
    const provisioning::StorageTopology topology{.generation = "topology-1", .devices = {device}};
    const Json topology_document = Json::parse(codec.encode(topology));
    expect_field("topology", topology_document, "schemaVersion", 1);
    expect_field("topology", topology_document, "generation", "topology-1");
    expect_field("topology candidate", topology_document.at("devices").at(0), "candidateId", "opaque-device");
    expect_field("topology system device", topology_document.at("devices").at(0), "systemDevice", true);
    expect_field("topology hotplug device", topology_document.at("devices").at(0), "hotplug", true);
    expect_field(
        "topology configured target",
        topology_document.at("devices").at(0).at("regions").at(0),
        "configuredBackupTarget",
        false
    );
    test_helpers::expect_true(
        "topology identity privacy",
        !topology_document.at("devices").at(0).contains("majorMinor"),
        "internal block identity was exposed"
    );

    const auto plan = provisioning::DevicePreparationPlanBuilder{}.build(
        topology,
        topology.generation,
        device.candidate_id,
        provisioning::ProvisioningMode::EraseWholeDevice,
        "plan-1",
        std::nullopt,
        provisioning::PlannedPartitionGeometry{.start_sector = 0, .sector_count = 2, .partition_number = 1}
    );
    const Json plan_document = Json::parse(codec.encode(plan));
    expect_field("plan", plan_document, "schemaVersion", 2);
    expect_field("plan", plan_document, "planId", "plan-1");
    expect_field("plan", plan_document, "mode", "erase-whole-device");
    test_helpers::expect_true(
        "plan layouts",
        plan_document.at("before").at("regions").size() == 2 &&
            plan_document.at("after").at("regions").size() == 1,
        "before or after layout is missing"
    );
    const auto partition_plan = provisioning::DevicePreparationPlanBuilder{}.build(
        topology,
        topology.generation,
        "opaque-partition",
        provisioning::ProvisioningMode::ReformatExistingPartition,
        "plan-2"
    );
    const Json partition_document = Json::parse(codec.encode(partition_plan));
    expect_field("partition plan", partition_document, "partitionId", "opaque-partition");
    expect_field("partition plan", partition_document, "destructiveScope", "existing-partition");
    test_helpers::expect_true(
        "partition operations",
        partition_document.at("operations").front() == "erase-partition-signatures",
        "partition signature erasure is not explicit"
    );
    const auto free_plan = provisioning::DevicePreparationPlanBuilder{}.build(
        topology,
        topology.generation,
        "opaque-free",
        provisioning::ProvisioningMode::CreatePartitionInUnallocatedSpace,
        "plan-3",
        std::nullopt,
        provisioning::PlannedPartitionGeometry{
            .start_sector = 2,
            .sector_count = 1,
            .partition_number = 2,
        }
    );
    const Json free_document = Json::parse(codec.encode(free_plan));
    expect_field("free-space plan", free_document, "freeRegionId", "opaque-free");
    expect_field("free-space plan", free_document, "destructiveScope", "unallocated-region");
    test_helpers::expect_true(
        "free-space operations",
        free_document.at("operations").front() == "backup-partition-table" &&
            free_document.at("operations").at(1) == "create-backup-partition",
        "free-space partition backup and creation are not explicit"
    );

    const provisioning::ExistingTargetInspection inspection{
        .inspection_id = "inspection-1",
        .topology_generation = "topology-1",
        .device_id = "opaque-device",
        .partition_id = "opaque-partition",
        .target = {
            .luks_uuid = "luks-uuid",
            .btrfs_uuid = "btrfs-uuid",
            .partition_uuid = "partition-uuid",
            .repository_id = "repository-1",
            .catalog_generation = 7,
            .snapshot_count = 2,
        },
    };
    const Json inspection_document = Json::parse(codec.encode(inspection));
    expect_field("target inspection", inspection_document, "schemaVersion", 2);
    expect_field("target inspection", inspection_document, "inspectionId", "inspection-1");
    expect_field("target inspection", inspection_document, "classification", "compatible-repository");
    expect_field("target inspection", inspection_document, "repositoryId", "repository-1");
    expect_field("target inspection", inspection_document, "snapshotCount", 2);
    test_helpers::expect_true(
        "target inspection privacy",
        !inspection_document.contains("path") && !inspection_document.contains("credential"),
        "target inspection exposed a path or credential"
    );
}

} // namespace

int main() {
    test_capabilities();
    test_profiles();
    test_status_history_and_device();
    test_storage_topology_and_plan_contract();
    return test_helpers::finish("manager JSON codec tests");
}
