// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerJsonCodec.hpp>

#include <ranges>
#include <utility>
#include <variant>

#include <config/json/JsonIo.hpp>
#include <core/ManagerProtocol.hpp>
#include <state/document/RunStatusDocumentCodec.hpp>

namespace btrfsbackup::daemon::dbus {
namespace {

config::json::Json blockers_json(const std::vector<provisioning::SafetyBlocker>& blockers) {
    config::json::Json result = config::json::Json::array();
    for (const auto& blocker : blockers)
        result.push_back({{"code", blocker.code}, {"detail", blocker.detail}});
    return result;
}

bool device_mounted(const provisioning::StorageDevice& device) {
    if (!device.mount_points.empty())
        return true;
    return std::ranges::any_of(device.regions, [](const auto& region) {
        const auto* partition = std::get_if<provisioning::ExistingPartition>(&region);
        return partition != nullptr && !partition->mount_points.empty();
    });
}

bool device_contains_data(const provisioning::StorageDevice& device) {
    return device.partition_table.type != provisioning::PartitionTableType::None ||
        !device.filesystem.type.empty() ||
        std::ranges::any_of(device.regions, [](const auto& region) {
               return std::holds_alternative<provisioning::ExistingPartition>(region);
           });
}

config::json::Json layout_json(const provisioning::StorageLayout& layout) {
    config::json::Json regions = config::json::Json::array();
    for (const auto& region : layout.regions) {
        regions.push_back({
            {"candidateId", region.id},
            {"kind", provisioning::predicted_region_kind_name(region.kind)},
            {"startSector", region.start_sector},
            {"sectorCount", region.sector_count},
            {"partitionNumber", region.partition_number},
            {"path", region.display_path},
            {"partitionLabel", region.partition_label},
            {"filesystemType", region.filesystem_type},
            {"geometryExact", region.geometry_exact},
            {"encrypted", region.encrypted},
            {"changed", region.changed},
            {"dataWillBeErased", region.data_will_be_erased},
        });
    }
    return {
        {"deviceId", layout.device_id},
        {"sizeBytes", layout.size_bytes},
        {"logicalSectorSize", layout.logical_sector_size},
        {"partitionTableType", provisioning::partition_table_type_name(layout.partition_table_type)},
        {"regions", std::move(regions)},
    };
}

std::string operation_name(const provisioning::PlannedStorageOperation& operation) {
    return std::visit(
        [](const auto& value) -> std::string {
            using Operation = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Operation, provisioning::BackupPartitionTable>)
                return "backup-partition-table";
            if constexpr (std::is_same_v<Operation, provisioning::EraseDeviceSignatures>)
                return "erase-device-signatures";
            if constexpr (std::is_same_v<Operation, provisioning::ErasePartitionSignatures>)
                return "erase-partition-signatures";
            if constexpr (std::is_same_v<Operation, provisioning::CreateGptPartitionTable>)
                return "create-gpt-partition-table";
            if constexpr (std::is_same_v<Operation, provisioning::CreateBackupPartition>)
                return "create-backup-partition";
            if constexpr (std::is_same_v<Operation, provisioning::FormatLuks2>)
                return "format-luks2";
            if constexpr (std::is_same_v<Operation, provisioning::OpenLuksMapping>)
                return "open-luks-mapping";
            if constexpr (std::is_same_v<Operation, provisioning::FormatBtrfs>)
                return "format-btrfs";
            if constexpr (std::is_same_v<Operation, provisioning::VerifyPreparedTarget>)
                return "verify-prepared-target";
            if constexpr (std::is_same_v<Operation, provisioning::PublishProfile>)
                return "publish-profile";
            return "unsupported";
        },
        operation
    );
}

std::string destructive_scope_name(provisioning::DestructiveScopeKind kind) {
    switch (kind) {
    case provisioning::DestructiveScopeKind::None:
        return "none";
    case provisioning::DestructiveScopeKind::WholeDevice:
        return "whole-device";
    case provisioning::DestructiveScopeKind::ExistingPartition:
        return "existing-partition";
    case provisioning::DestructiveScopeKind::UnallocatedRegion:
        return "unallocated-region";
    }
    return "unsupported";
}

} // namespace

std::string ManagerJsonCodec::encode(const ManagerCapabilities& capabilities) const {
    return config::json::dump_json({
        {"schemaVersion", manager_protocol::capabilities_schema_version},
        {"interface", capabilities.interface_name},
        {"apiMajor", capabilities.api_major},
        {"apiMinor", capabilities.api_minor},
        {"profileSchemaVersion", capabilities.profile_schema_version},
        {"publicStatusSchemaVersion", capabilities.public_status_schema_version},
        {"historySchemaVersion", capabilities.history_schema_version},
        {"deviceStateSchemaVersion", capabilities.device_state_schema_version},
        {"readOnly", capabilities.read_only},
        {"features", capabilities.features},
    });
}

std::string ManagerJsonCodec::encode(const std::vector<ProfileSummary>& profiles) const {
    config::json::Json result = config::json::Json::array();
    for (const auto& profile : profiles) {
        config::json::Json sources = config::json::Json::array();
        for (const auto& source : profile.sources)
            sources.push_back({{"id", source.id}, {"name", source.name}});
        result.push_back({
            {"schemaVersion", manager_protocol::profile_summary_schema_version},
            {"profileId", profile.profile_id},
            {"name", profile.name},
            {"enabled", profile.enabled},
            {"targetName", profile.target_name},
            {"sources", std::move(sources)},
            {"configurationValid", profile.configuration_valid},
            {"configurationErrorCode", profile.configuration_error_code},
        });
    }
    return config::json::dump_json(result);
}

std::string ManagerJsonCodec::encode(const PublicStatusResponse& status) const {
    config::json::Json result = config::json::Json::parse(
        state::document::RunStatusDocumentCodec{}.serialize_public(status.run)
    );
    result["schemaVersion"] = manager_protocol::public_status_schema_version;
    result["sourceIndex"] = status.source_index;
    result["sourceCount"] = status.source_count;
    result["startedAt"] = status.started_at;
    result["updatedAt"] = status.updated_at;
    result["lastSuccessAt"] = status.last_success_at;
    result["lastAttemptAt"] = status.last_attempt_at;
    result["lastAttemptState"] = status.last_attempt_state;
    return config::json::dump_json(result);
}

std::string ManagerJsonCodec::encode(const SanitizedHistoryPage& page) const {
    config::json::Json result = config::json::Json::array();
    for (const auto& entry : page.entries) {
        result.push_back({
            {"schemaVersion", manager_protocol::history_schema_version},
            {"state", entry.state},
            {"errorCode", entry.error_code},
            {"sourceName", entry.source_name},
            {"targetName", entry.target_name},
            {"startedAt", entry.started_at},
            {"finishedAt", entry.finished_at},
            {"sourceCount", entry.source_count},
            {"overallProgress", entry.overall_progress},
            {"bytesTransferred", entry.bytes_transferred},
        });
    }
    return config::json::dump_json(result);
}

std::string ManagerJsonCodec::encode(const TargetStatus& status) const {
    return state::document::TargetStatusDocumentCodec{}.serialize(status);
}

std::string ManagerJsonCodec::encode(const OperationResult& result) const {
    config::json::Json document{
        {"schemaVersion", manager_protocol::operation_result_schema_version},
        {"operation", result.operation},
        {"operationId", result.operation_id},
        {"profileId", result.profile_id},
        {"accepted", result.accepted},
    };
    if (!result.run_id.empty())
        document["runId"] = result.run_id;
    return config::json::dump_json(document);
}

std::string ManagerJsonCodec::encode(const BrowseSessionInfo& session) const {
    return config::json::dump_json({
        {"schemaVersion", manager_protocol::browse_session_schema_version},
        {"sessionId", session.session_id},
        {"profileId", session.profile_id},
        {"rootPath", session.root_path},
        {"expiresAt", session.expires_at},
        {"readOnly", session.read_only},
    });
}

std::string ManagerJsonCodec::encode(const std::vector<BackupCoverage>& coverage) const {
    config::json::Json result = config::json::Json::array();
    for (const auto& item : coverage) {
        result.push_back({
            {"profileId", item.profile_id},
            {"sourceId", item.source_id},
            {"relativePath", item.relative_path},
        });
    }
    return config::json::dump_json(result);
}

std::string ManagerJsonCodec::encode(const control::ProfileDetails& profile) const {
    config::json::Json document = config::json::Json::parse(profile.document);
    document.erase("hooks");
    if (document.contains("target") && document["target"].is_object()) {
        auto& target = document["target"];
        if (target.contains("activation") && target["activation"].is_object())
            target["activation"].erase("keyFile");
    }
    return config::json::dump_json({
        {"schemaVersion", manager_protocol::profile_details_schema_version},
        {"profileId", profile.profile_id},
        {"generation", profile.generation},
        {"fingerprint", profile.fingerprint},
        {"document", std::move(document)},
        {"configurationValid", profile.configuration_valid},
        {"configurationErrorCode", profile.configuration_error_code},
        {"sourceCandidates", profile.source_candidates},
    });
}

std::string ManagerJsonCodec::encode(const std::vector<control::TargetCredential>& credentials) const {
    config::json::Json result = config::json::Json::array();
    for (const auto& credential : credentials) {
        result.push_back({
            {"schemaVersion", manager_protocol::target_credentials_schema_version},
            {"id", credential.id},
            {"label", credential.label},
            {"type", credential.type},
            {"keyslot", credential.keyslot},
            {"managed", credential.managed},
            {"automatic", credential.automatic},
        });
    }
    return config::json::dump_json(result);
}

std::string ManagerJsonCodec::encode(const provisioning::StorageTopology& topology) const {
    config::json::Json devices = config::json::Json::array();
    for (const auto& device : topology.devices) {
        config::json::Json regions = config::json::Json::array();
        for (const auto& region : device.regions) {
            std::visit(
                [&](const auto& value) {
                    using Region = std::decay_t<decltype(value)>;
                    config::json::Json item{
                        {"candidateId", [&] {
                             if constexpr (std::is_same_v<Region, provisioning::ExistingPartition>)
                                 return value.candidate_id;
                             else
                                 return value.id;
                         }()},
                        {"kind", std::is_same_v<Region, provisioning::ExistingPartition> ? "existing-partition" : "unallocated"},
                        {"startSector", value.start_sector},
                        {"sectorCount", value.sector_count},
                        {"blockers", blockers_json(value.blockers)},
                    };
                    if constexpr (std::is_same_v<Region, provisioning::ExistingPartition>) {
                        item["path"] = value.identity.display_path;
                        item["partitionNumber"] = value.partition_number;
                        item["partitionUuid"] = value.partition_uuid;
                        item["partitionLabel"] = value.partition_label;
                        item["filesystemType"] = value.filesystem.type;
                        item["filesystemLabel"] = value.filesystem.label;
                        item["filesystemUuid"] = value.filesystem.uuid;
                        item["mountPoints"] = value.mount_points;
                        item["suitableForReformat"] = value.suitable_for_reformat;
                        item["suitableForAdoption"] = value.suitable_for_adoption;
                    } else {
                        item["suitableForBackupPartition"] = value.suitable_for_backup_partition;
                    }
                    regions.push_back(std::move(item));
                },
                region
            );
        }
        devices.push_back({
            {"candidateId", device.candidate_id},
            {"path", device.identity.display_path},
            {"displayName", device.display_name},
            {"model", device.display_name},
            {"transport", device.transport},
            {"sizeBytes", device.size_bytes},
            {"logicalSectorSize", device.logical_sector_size},
            {"physicalSectorSize", device.physical_sector_size},
            {"removable", device.removable},
            {"mounted", device_mounted(device)},
            {"containsData", device_contains_data(device)},
            {"readOnly", device.read_only},
            {"partitionTableType", provisioning::partition_table_type_name(device.partition_table.type)},
            {"regions", std::move(regions)},
            {"blockers", blockers_json(device.blockers)},
        });
    }
    return config::json::dump_json({
        {"schemaVersion", manager_protocol::storage_topology_schema_version},
        {"generation", topology.generation},
        {"devices", std::move(devices)},
    });
}

std::string ManagerJsonCodec::encode(const provisioning::DevicePreparationPlan& plan) const {
    config::json::Json operations = config::json::Json::array();
    for (const auto& operation : plan.operations)
        operations.push_back(operation_name(operation));
    config::json::Json warnings = config::json::Json::array();
    for (const auto& warning : plan.warnings)
        warnings.push_back({{"code", warning.code}, {"detail", warning.detail}});
    return config::json::dump_json({
        {"schemaVersion", manager_protocol::device_preparation_plan_schema_version},
        {"planId", plan.id},
        {"topologyGeneration", plan.topology_generation},
        {"mode", provisioning::provisioning_mode_name(plan.mode)},
        {"deviceId", plan.device_id},
        {"partitionId", plan.partition_id},
        {"freeRegionId", plan.free_region_id},
        {"inspectionId", plan.inspection_id},
        {"before", layout_json(plan.before)},
        {"after", layout_json(plan.after)},
        {"operations", std::move(operations)},
        {"warnings", std::move(warnings)},
        {"destructiveScope", destructive_scope_name(plan.destructive_scope.kind)},
    });
}

std::string ManagerJsonCodec::encode(const provisioning::ExistingTargetInspection& inspection) const {
    return config::json::dump_json({
        {"schemaVersion", manager_protocol::existing_target_inspection_schema_version},
        {"inspectionId", inspection.inspection_id},
        {"topologyGeneration", inspection.topology_generation},
        {"deviceId", inspection.device_id},
        {"partitionId", inspection.partition_id},
        {"classification", provisioning::existing_target_classification_name(inspection.target.classification)},
        {"diagnosticCode", inspection.target.diagnostic_code},
        {"luksUuid", inspection.target.luks_uuid},
        {"btrfsUuid", inspection.target.btrfs_uuid},
        {"partitionUuid", inspection.target.partition_uuid},
        {"repositoryId", inspection.target.repository_id},
        {"catalogGeneration", inspection.target.catalog_generation},
        {"snapshotCount", inspection.target.snapshot_count},
    });
}

std::string ManagerJsonCodec::encode(const control::DevicePreparationStatus& status) const {
    return config::json::dump_json({
        {"schemaVersion", manager_protocol::device_provisioning_schema_version},
        {"operationId", status.operation_id},
        {"profileId", status.profile_id},
        {"state", status.state},
        {"phase", status.phase},
        {"errorCode", status.error_code},
        {"recoveryAction", status.recovery_action},
        {"canCancel", status.can_cancel},
    });
}

} // namespace btrfsbackup::daemon::dbus
