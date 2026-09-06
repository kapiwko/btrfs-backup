// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DevicePreparationTransactionCodec.hpp>

#include <ranges>
#include <string_view>

#include <config/json/Json.hpp>
#include <core/Errors.hpp>

namespace btrfsbackup::daemon::control {
namespace {

using config::json::Json;
using provisioning::DevicePreparationTarget;
using provisioning::ProvisioningDevice;

Json source_json(const provisioning::DevicePreparationSource& source) {
    return {
        {"name", source.name},
        {"subvolume", source.subvolume},
        {"filesystemUuid", source.filesystem_uuid},
        {"mountRoot", source.mount_root},
        {"localSnapshotDir", source.local_snapshot_dir},
        {"localRetention", source.local_retention},
        {"remoteRetention", source.remote_retention},
    };
}

provisioning::DevicePreparationSource parse_source(const Json& source) {
    return {
        .candidate_id = {},
        .name = source.value("name", ""),
        .subvolume = source.value("subvolume", ""),
        .filesystem_uuid = source.value("filesystemUuid", ""),
        .mount_root = source.value("mountRoot", ""),
        .local_snapshot_dir = source.value("localSnapshotDir", ""),
        .local_retention = source.value("localRetention", std::size_t{30}),
        .remote_retention = source.value("remoteRetention", std::size_t{30}),
    };
}

Json device_json(const ProvisioningDevice& device) {
    return {
        {"path", device.path},
        {"model", device.model},
        {"serial", device.serial},
        {"transport", device.transport},
        {"sizeBytes", device.size_bytes},
        {"removable", device.removable},
        {"mounted", device.mounted},
        {"containsData", device.contains_data},
        {"majorMinor", device.major_minor},
        {"sysfsDevpath", device.sysfs_devpath},
        {"wwn", device.wwn},
        {"serialId", device.serial_id},
        {"serialShort", device.serial_short},
        {"deviceGraph", device.device_graph},
    };
}

ProvisioningDevice parse_device(const Json& value) {
    return {
        .path = value.value("path", ""),
        .model = value.value("model", ""),
        .serial = value.value("serial", ""),
        .transport = value.value("transport", ""),
        .size_bytes = value.value("sizeBytes", std::uint64_t{0}),
        .removable = value.value("removable", false),
        .mounted = value.value("mounted", false),
        .contains_data = value.value("containsData", false),
        .major_minor = value.value("majorMinor", ""),
        .sysfs_devpath = value.value("sysfsDevpath", ""),
        .wwn = value.value("wwn", ""),
        .serial_id = value.value("serialId", ""),
        .serial_short = value.value("serialShort", ""),
        .device_graph = value.value("deviceGraph", ""),
    };
}

Json identity_json(const provisioning::StableBlockDeviceIdentity& identity) {
    return {
        {"path", identity.display_path},
        {"majorMinor", identity.major_minor},
        {"sysfsPath", identity.sysfs_path},
        {"wwn", identity.wwn},
        {"serial", identity.serial},
        {"serialShort", identity.serial_short},
        {"sizeBytes", identity.size_bytes},
    };
}

provisioning::StableBlockDeviceIdentity parse_identity(const Json& value) {
    return {
        .display_path = value.value("path", ""),
        .major_minor = value.value("majorMinor", ""),
        .sysfs_path = value.value("sysfsPath", ""),
        .wwn = value.value("wwn", ""),
        .serial = value.value("serial", ""),
        .serial_short = value.value("serialShort", ""),
        .size_bytes = value.value("sizeBytes", std::uint64_t{0}),
    };
}

Json filesystem_json(const provisioning::FilesystemDescription& filesystem) {
    return {{"type", filesystem.type}, {"version", filesystem.version}, {"label", filesystem.label}, {"uuid", filesystem.uuid}};
}

provisioning::FilesystemDescription parse_filesystem(const Json& value) {
    return {
        .type = value.value("type", ""),
        .version = value.value("version", ""),
        .label = value.value("label", ""),
        .uuid = value.value("uuid", ""),
    };
}

std::optional<std::string> optional_string(const Json& value, const char* key) {
    if (!value.contains(key) || value.at(key).is_null())
        return std::nullopt;
    return value.at(key).get<std::string>();
}

Json target_json(const DevicePreparationTarget& target) {
    Json partition = nullptr;
    if (target.partition.has_value()) {
        partition = {
            {"identity", identity_json(target.partition->identity)},
            {"partitionUuid", target.partition->partition_uuid},
            {"partitionLabel", target.partition->partition_label},
            {"partitionNumber", target.partition->partition_number},
            {"startSector", target.partition->start_sector},
            {"sectorCount", target.partition->sector_count},
            {"filesystem", filesystem_json(target.partition->filesystem)},
            {"suitableForReformat", target.partition->suitable_for_reformat},
            {"suitableForAdoption", target.partition->suitable_for_adoption},
        };
    }
    Json expected_inspection = nullptr;
    if (target.expected_inspection.has_value()) {
        expected_inspection = {
            {"luksUuid", target.expected_inspection->luks_uuid},
            {"btrfsUuid", target.expected_inspection->btrfs_uuid},
            {"partitionUuid", target.expected_inspection->partition_uuid},
            {"repositoryId", target.expected_inspection->repository_id},
            {"catalogGeneration", target.expected_inspection->catalog_generation},
            {"snapshotCount", target.expected_inspection->snapshot_count},
        };
    }
    Json free_region = nullptr;
    if (target.free_region.has_value()) {
        free_region = {
            {"startSector", target.free_region->start_sector},
            {"sectorCount", target.free_region->sector_count},
        };
    }
    Json planned_geometry = nullptr;
    if (target.planned_partition_geometry.has_value()) {
        planned_geometry = {
            {"startSector", target.planned_partition_geometry->start_sector},
            {"sectorCount", target.planned_partition_geometry->sector_count},
            {"partitionNumber", target.planned_partition_geometry->partition_number},
        };
    }
    return {
        {"mode", provisioning::provisioning_mode_name(target.mode)},
        {"deviceIdentity", identity_json(target.device.identity)},
        {"transport", target.device.transport},
        {"logicalSectorSize", target.device.logical_sector_size},
        {"physicalSectorSize", target.device.physical_sector_size},
        {"partitionTableType", provisioning::partition_table_type_name(target.device.partition_table.type)},
        {"partitionTableId", target.device.partition_table.identifier},
        {"partition", std::move(partition)},
        {"freeRegion", std::move(free_region)},
        {"plannedPartitionGeometry", std::move(planned_geometry)},
        {"expectedInspection", std::move(expected_inspection)},
    };
}

provisioning::PartitionTableType parse_partition_table_type(const std::string& value) {
    if (value == "gpt")
        return provisioning::PartitionTableType::Gpt;
    if (value == "mbr")
        return provisioning::PartitionTableType::Mbr;
    if (value == "none")
        return provisioning::PartitionTableType::None;
    return provisioning::PartitionTableType::Unsupported;
}

DevicePreparationTarget parse_target(const Json& value) {
    DevicePreparationTarget result;
    const std::string mode = value.value("mode", "");
    const auto parsed_mode = provisioning::provisioning_mode_from_name(mode);
    if (!parsed_mode.has_value())
        throw ValidationError("unsupported device preparation transaction mode");
    result.mode = *parsed_mode;
    result.device.identity = parse_identity(value.at("deviceIdentity"));
    result.device.transport = value.value("transport", "");
    result.device.size_bytes = result.device.identity.size_bytes;
    result.device.logical_sector_size = value.value("logicalSectorSize", std::uint32_t{0});
    result.device.physical_sector_size = value.value("physicalSectorSize", std::uint32_t{0});
    result.device.partition_table = {
        .type = parse_partition_table_type(value.value("partitionTableType", "unsupported")),
        .identifier = value.value("partitionTableId", ""),
    };
    if (value.contains("partition") && value.at("partition").is_object()) {
        const auto& partition = value.at("partition");
        result.partition = provisioning::ExistingPartition{
            .candidate_id = {},
            .identity = parse_identity(partition.at("identity")),
            .partition_uuid = optional_string(partition, "partitionUuid"),
            .partition_label = optional_string(partition, "partitionLabel"),
            .partition_number = partition.value("partitionNumber", std::uint32_t{0}),
            .start_sector = partition.value("startSector", std::uint64_t{0}),
            .sector_count = partition.value("sectorCount", std::uint64_t{0}),
            .filesystem = parse_filesystem(partition.at("filesystem")),
            .mount_points = {},
            .holders = {},
            .blockers = {},
            .suitable_for_reformat = partition.value("suitableForReformat", false),
            .suitable_for_adoption = partition.value("suitableForAdoption", false),
        };
    }
    if (value.contains("expectedInspection") && value.at("expectedInspection").is_object()) {
        const auto& inspection = value.at("expectedInspection");
        result.expected_inspection = provisioning::ExistingTargetInspectionSummary{
            .classification = provisioning::ExistingTargetClassification::CompatibleRepository,
            .diagnostic_code = {},
            .luks_uuid = inspection.value("luksUuid", ""),
            .btrfs_uuid = inspection.value("btrfsUuid", ""),
            .partition_uuid = inspection.value("partitionUuid", ""),
            .repository_id = inspection.value("repositoryId", ""),
            .catalog_generation = inspection.value("catalogGeneration", std::uint64_t{0}),
            .snapshot_count = inspection.value("snapshotCount", std::size_t{0}),
        };
    }
    if (value.contains("freeRegion") && value.at("freeRegion").is_object()) {
        const auto& free_region = value.at("freeRegion");
        provisioning::UnallocatedRegion parsed_free_region;
        parsed_free_region.start_sector = free_region.value("startSector", std::uint64_t{0});
        parsed_free_region.sector_count = free_region.value("sectorCount", std::uint64_t{0});
        result.free_region = std::move(parsed_free_region);
    }
    if (value.contains("plannedPartitionGeometry") && value.at("plannedPartitionGeometry").is_object()) {
        const auto& geometry = value.at("plannedPartitionGeometry");
        result.planned_partition_geometry = provisioning::PlannedPartitionGeometry{
            .start_sector = geometry.value("startSector", std::uint64_t{0}),
            .sector_count = geometry.value("sectorCount", std::uint64_t{0}),
            .partition_number = geometry.value("partitionNumber", std::uint32_t{0}),
        };
    }
    if (result.device.identity.major_minor.empty() || result.device.identity.sysfs_path.empty() ||
        result.device.identity.size_bytes == 0 || result.device.logical_sector_size == 0 ||
        result.device.transport.empty() ||
        ((result.mode == provisioning::ProvisioningMode::ReformatExistingPartition ||
          result.mode == provisioning::ProvisioningMode::AdoptExistingTarget) &&
         !result.partition.has_value()) ||
        (result.mode == provisioning::ProvisioningMode::AdoptExistingTarget &&
         (!result.expected_inspection.has_value() || result.expected_inspection->luks_uuid.empty() ||
          result.expected_inspection->btrfs_uuid.empty() || result.expected_inspection->partition_uuid.empty() ||
          result.expected_inspection->repository_id.empty())) ||
        (result.mode == provisioning::ProvisioningMode::CreatePartitionInUnallocatedSpace &&
         (!result.free_region.has_value() || result.free_region->sector_count == 0 ||
          !result.planned_partition_geometry.has_value() || result.planned_partition_geometry->sector_count == 0 ||
          result.planned_partition_geometry->partition_number == 0)))
        throw ValidationError("incomplete device preparation target snapshot");
    if (result.mode == provisioning::ProvisioningMode::EraseWholeDevice &&
        (!result.planned_partition_geometry.has_value() || result.planned_partition_geometry->sector_count == 0 ||
         result.planned_partition_geometry->partition_number == 0))
        throw ValidationError("incomplete device preparation target snapshot");
    return result;
}

Json transaction_json(const DevicePreparationTransaction& transaction) {
    return {
        {"schemaVersion", 1},
        {"revision", transaction.revision.value},
        {"operationId", transaction.status.operation_id},
        {"profileId", transaction.status.profile_id},
        {"state", transaction.status.state},
        {"phase", transaction.status.phase},
        {"errorCode", transaction.status.error_code},
        {"recoveryAction", transaction.status.recovery_action},
        {"canCancel", transaction.status.can_cancel},
        {"ownerBusName", transaction.owner.bus_name},
        {"ownerUid", transaction.owner.uid},
        {"device", device_json(transaction.device)},
        {"target", target_json(transaction.target)},
        {"profileName", transaction.profile_name},
        {"sources", [&] {
             Json sources = Json::array();
             for (const auto& source : transaction.sources)
                 sources.push_back(source_json(source));
             return sources;
         }()},
        {"passphraseLabel", transaction.passphrase_label},
        {"createAutomaticKey", transaction.create_automatic_key},
        {"createdAt", transaction.created_at},
        {"updatedAt", transaction.updated_at},
        {"lastCompletedPhase", transaction.last_completed_phase},
        {"partitionTableBackup", transaction.partition_table_backup},
        {"partition", transaction.partition},
        {"partitionDeviceNumber", transaction.partition_device_number},
        {"partitionUuid", transaction.partition_uuid},
        {"luksUuid", transaction.luks_uuid},
        {"btrfsUuid", transaction.btrfs_uuid},
        {"mapper", transaction.mapper},
        {"mapperDeviceNumber", transaction.mapper_device_number},
        {"inspectionMountPoint", transaction.inspection_mount_point},
        {"configurationState", transaction.configuration_state},
        {"credentialsState", transaction.credentials_state},
        {"profileReservationState", transaction.profile_reservation_state},
        {"cleanupResult", transaction.cleanup_result},
        {"cancelRequested", transaction.cancel_requested},
        {"requestedDeviceAccess", transaction.requested_device_access},
        {"requestedMapperControl", transaction.requested_mapper_control},
        {"accessGeneration", transaction.access_generation},
        {"authorizedAccessGeneration", transaction.authorized_access_generation},
    };
}

DevicePreparationTransaction parse_transaction(const Json& value) {
    const int schema_version = value.value("schemaVersion", 0);
    if (!value.is_object() || schema_version != 1 || !value.contains("device"))
        throw ValidationError("invalid device preparation transaction");
    DevicePreparationTransaction result;
    result.revision.value = value.value("revision", std::uint64_t{0});
    result.status = {
        .operation_id = value.value("operationId", ""),
        .profile_id = value.value("profileId", ""),
        .state = value.value("state", ""),
        .phase = value.value("phase", ""),
        .error_code = value.value("errorCode", ""),
        .recovery_action = value.value("recoveryAction", ""),
        .last_completed_phase = {},
        .cleanup_result = "not-required",
        .can_cancel = value.value("canCancel", false),
    };
    result.owner = {
        .bus_name = value.value("ownerBusName", ""),
        .uid = value.value("ownerUid", std::uint32_t{0}),
    };
    result.device = parse_device(value.at("device"));
    if (!value.contains("target") || !value.at("target").is_object())
        throw ValidationError("missing device preparation target snapshot");
    result.target = parse_target(value.at("target"));
    result.profile_name = value.value("profileName", "");
    if (value.contains("sources") && value.at("sources").is_array()) {
        for (const auto& source : value.at("sources"))
            result.sources.push_back(parse_source(source));
    }
    if (result.sources.empty()) {
        result.sources.push_back({
            .candidate_id = {},
            .name = "Source",
            .subvolume = value.value("sourceSubvolume", ""),
            .filesystem_uuid = value.value("sourceFilesystemUuid", ""),
            .mount_root = value.value("sourceMountRoot", ""),
            .local_snapshot_dir = value.value("localSnapshotDir", ""),
            .local_retention = 30,
            .remote_retention = 30,
        });
    }
    result.passphrase_label = value.value("passphraseLabel", "");
    result.create_automatic_key = value.value("createAutomaticKey", true);
    result.created_at = value.value("createdAt", std::int64_t{0});
    result.updated_at = value.value("updatedAt", std::int64_t{0});
    result.last_completed_phase = value.value("lastCompletedPhase", "");
    result.partition_table_backup = value.value("partitionTableBackup", "");
    result.partition = value.value("partition", "");
    result.partition_device_number = value.value("partitionDeviceNumber", "");
    result.partition_uuid = value.value("partitionUuid", "");
    result.luks_uuid = value.value("luksUuid", "");
    result.btrfs_uuid = value.value("btrfsUuid", "");
    result.mapper = value.value("mapper", "");
    result.mapper_device_number = value.value("mapperDeviceNumber", "");
    result.inspection_mount_point = value.value("inspectionMountPoint", "");
    result.configuration_state = value.value("configurationState", "not-started");
    result.credentials_state = value.value("credentialsState", "not-started");
    result.profile_reservation_state = value.value("profileReservationState", "not-held");
    result.cleanup_result = value.value("cleanupResult", "not-required");
    result.cancel_requested = value.value("cancelRequested", false);
    result.requested_device_access = value.value("requestedDeviceAccess", std::vector<std::string>{});
    result.requested_mapper_control = value.value("requestedMapperControl", false);
    result.access_generation = value.value("accessGeneration", std::uint64_t{0});
    result.authorized_access_generation = value.value("authorizedAccessGeneration", std::uint64_t{0});
    if (result.revision.value == 0 || result.status.operation_id.empty() || result.status.profile_id.empty() ||
        result.owner.bus_name.empty() || result.created_at <= 0 ||
        result.profile_name.empty() || result.passphrase_label.empty() || result.sources.empty() ||
        std::ranges::any_of(result.sources, [](const auto& source) {
            return source.name.empty() || source.subvolume.empty() || source.filesystem_uuid.empty() ||
                source.mount_root.empty() || source.local_snapshot_dir.empty() ||
                source.local_retention == 0 || source.local_retention > 100000 ||
                source.remote_retention > 100000;
        }) || result.requested_device_access.empty() ||
        result.access_generation == 0 ||
        result.authorized_access_generation > result.access_generation)
        throw ValidationError("incomplete device preparation transaction");
    return result;
}

} // namespace

std::string DevicePreparationTransactionCodec::serialize(
    const DevicePreparationTransaction& transaction
) const {
    return transaction_json(transaction).dump(2) + "\n";
}

DevicePreparationTransaction DevicePreparationTransactionCodec::deserialize(
    std::string_view document
) const {
    return parse_transaction(Json::parse(document));
}

} // namespace btrfsbackup::daemon::control
