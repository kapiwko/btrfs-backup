// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DevicePreparationTransaction.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <sys/stat.h>

#include <config/json/Json.hpp>
#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <platform/linux/filesystem/FileIo.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

using config::json::Json;

constexpr mode_t transaction_permissions = S_IRUSR | S_IWUSR;
constexpr fs::perms transaction_directory_permissions =
    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec;

bool terminal(const DevicePreparationTransaction& transaction) {
    return transaction.status.state == "succeeded" || transaction.status.state == "failed" ||
        transaction.status.state == "cancelled" || transaction.status.state == "interrupted";
}

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    )
        .count();
}

void prepare_root(const fs::path& root) {
    std::error_code error;
    fs::create_directories(root, error);
    if (error)
        throw ValidationError("cannot create device preparation transaction directory");
    fs::permissions(root, transaction_directory_permissions, fs::perm_options::replace, error);
    if (error)
        throw ValidationError("cannot secure device preparation transaction directory");
}

Json device_json(const ProvisioningDevice& device) {
    return {
        {"path", device.path},
        {"model", device.model},
        {"serial", device.serial},
        {"transport", device.transport},
        {"sizeBytes", device.size_bytes},
        {"removable", device.removable},
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
        .mounted = false,
        .contains_data = false,
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
    if (mode == "erase-whole-device")
        result.mode = provisioning::ProvisioningMode::EraseWholeDevice;
    else if (mode == "reformat-existing-partition")
        result.mode = provisioning::ProvisioningMode::ReformatExistingPartition;
    else
        throw ValidationError("unsupported device preparation transaction mode");
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
        };
    }
    if (result.device.identity.major_minor.empty() || result.device.identity.sysfs_path.empty() ||
        result.device.identity.size_bytes == 0 || result.device.logical_sector_size == 0 ||
        result.device.transport.empty() ||
        (result.mode == provisioning::ProvisioningMode::ReformatExistingPartition && !result.partition.has_value()))
        throw ValidationError("incomplete device preparation target snapshot");
    return result;
}

Json transaction_json(const DevicePreparationTransaction& transaction) {
    return {
        {"schemaVersion", 3},
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
        {"sourceSubvolume", transaction.source_subvolume},
        {"passphraseLabel", transaction.passphrase_label},
        {"createAutomaticKey", transaction.create_automatic_key},
        {"createdAt", transaction.created_at},
        {"updatedAt", transaction.updated_at},
        {"lastCompletedPhase", transaction.last_completed_phase},
        {"partition", transaction.partition},
        {"partitionUuid", transaction.partition_uuid},
        {"luksUuid", transaction.luks_uuid},
        {"btrfsUuid", transaction.btrfs_uuid},
        {"mapper", transaction.mapper},
        {"configurationState", transaction.configuration_state},
        {"credentialsState", transaction.credentials_state},
        {"cleanupResult", transaction.cleanup_result},
    };
}

DevicePreparationTransaction parse_transaction(const Json& value) {
    const int schema_version = value.value("schemaVersion", 0);
    if (!value.is_object() || schema_version != 3 || !value.contains("device"))
        throw ValidationError("invalid device preparation transaction");
    DevicePreparationTransaction result;
    result.status = {
        .operation_id = value.value("operationId", ""),
        .profile_id = value.value("profileId", ""),
        .state = value.value("state", ""),
        .phase = value.value("phase", ""),
        .error_code = value.value("errorCode", ""),
        .recovery_action = value.value("recoveryAction", ""),
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
    result.source_subvolume = value.value("sourceSubvolume", "");
    result.passphrase_label = value.value("passphraseLabel", "");
    result.create_automatic_key = value.value("createAutomaticKey", true);
    result.created_at = value.value("createdAt", std::int64_t{0});
    result.updated_at = value.value("updatedAt", std::int64_t{0});
    result.last_completed_phase = value.value("lastCompletedPhase", "");
    result.partition = value.value("partition", "");
    result.partition_uuid = value.value("partitionUuid", "");
    result.luks_uuid = value.value("luksUuid", "");
    result.btrfs_uuid = value.value("btrfsUuid", "");
    result.mapper = value.value("mapper", "");
    result.configuration_state = value.value("configurationState", "not-started");
    result.credentials_state = value.value("credentialsState", "not-started");
    result.cleanup_result = value.value("cleanupResult", "not-required");
    if (result.status.operation_id.empty() || result.status.profile_id.empty() ||
        result.owner.bus_name.empty() || result.created_at <= 0 ||
        result.profile_name.empty() || result.source_subvolume.empty() || result.passphrase_label.empty())
        throw ValidationError("incomplete device preparation transaction");
    return result;
}

} // namespace

DevicePreparationTransactionStore::DevicePreparationTransactionStore(
    fs::path root,
    std::size_t completed_limit,
    std::chrono::seconds completed_ttl
) : root_(std::move(root)), completed_limit_(completed_limit), completed_ttl_(completed_ttl) {
    if (root_.empty() || completed_limit_ == 0 || completed_ttl_ <= std::chrono::seconds::zero())
        throw std::invalid_argument("invalid device preparation transaction retention");
}

void DevicePreparationTransactionStore::save(const DevicePreparationTransaction& transaction) const {
    if (transaction.status.operation_id.empty())
        throw ValidationError("device preparation transaction has no operation identifier");
    validate_operation_id(transaction.status.operation_id);
    prepare_root(root_);
    platform::linux::filesystem::atomic_write(
        root_ / (transaction.status.operation_id + ".json"),
        transaction_json(transaction).dump(2) + "\n",
        transaction_permissions
    );
}

DevicePreparationTransaction DevicePreparationTransactionStore::load(
    const std::string& operation_id
) const {
    validate_operation_id(operation_id);
    prepare_root(root_);
    const fs::path path = root_ / (operation_id + ".json");
    std::ifstream input(path);
    if (!input)
        throw ValidationError("device preparation transaction not found");
    Json document;
    input >> document;
    DevicePreparationTransaction transaction = parse_transaction(document);
    if (transaction.status.operation_id != operation_id)
        throw ValidationError("transaction file name does not match its operation identifier");
    return transaction;
}

std::vector<DevicePreparationTransaction> DevicePreparationTransactionStore::load_and_prune() const {
    std::vector<DevicePreparationTransaction> transactions;
    prepare_root(root_);
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(root_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        try {
            std::ifstream input(entry.path());
            Json document;
            input >> document;
            DevicePreparationTransaction transaction = parse_transaction(document);
            if (entry.path().stem() != transaction.status.operation_id)
                throw ValidationError("transaction file name does not match its operation identifier");
            transactions.push_back(std::move(transaction));
        } catch (const std::exception& exception) {
            throw ValidationError(
                "cannot load device preparation transaction " + entry.path().filename().string() +
                ": " + exception.what()
            );
        }
    }

    const std::int64_t cutoff = now_seconds() - completed_ttl_.count();
    std::vector<DevicePreparationTransaction*> completed;
    for (auto& transaction : transactions)
        if (terminal(transaction))
            completed.push_back(&transaction);
    std::ranges::sort(completed, std::greater{}, &DevicePreparationTransaction::updated_at);
    bool removed = false;
    for (std::size_t index = 0; index < completed.size(); ++index) {
        DevicePreparationTransaction& transaction = *completed.at(index);
        if (transaction.updated_at < cutoff || index >= completed_limit_) {
            fs::remove(root_ / (transaction.status.operation_id + ".json"), error);
            if (error)
                throw ValidationError("cannot prune device preparation transaction");
            removed = true;
            transaction.status.operation_id.clear();
        }
    }
    if (removed)
        platform::linux::filesystem::fsync_dir(root_);
    std::erase_if(transactions, [](const auto& transaction) { return transaction.status.operation_id.empty(); });
    return transactions;
}

} // namespace btrfsbackup::daemon::control
