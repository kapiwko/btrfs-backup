// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DevicePreparationTransaction.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <config/json/Json.hpp>
#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <platform/linux/filesystem/FileIo.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>
#include <state/document/BoundedDocumentReader.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

using config::json::Json;

constexpr mode_t transaction_permissions = S_IRUSR | S_IWUSR;
constexpr std::size_t maximum_transaction_size = 1024 * 1024;
constexpr fs::perms transaction_directory_permissions =
    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec;

class TransactionLock final {
  public:
    explicit TransactionLock(const fs::path& path) {
        int descriptor;
        do {
            descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, transaction_permissions);
        } while (descriptor < 0 && errno == EINTR);
        if (descriptor < 0)
            throw ValidationError("cannot open device preparation transaction lock");
        descriptor_ = platform::linux::OwnedFileDescriptor(descriptor);
        int result;
        do {
            result = ::flock(descriptor_.get(), LOCK_EX);
        } while (result < 0 && errno == EINTR);
        if (result < 0)
            throw ValidationError("cannot lock device preparation transaction");
    }

  private:
    platform::linux::OwnedFileDescriptor descriptor_;
};

bool terminal(const DevicePreparationTransaction& transaction) {
    return transaction.profile_reservation_state != "held" &&
        transaction.profile_reservation_state != "releasing" &&
        (transaction.status.state == "succeeded" || transaction.status.state == "failed" ||
         transaction.status.state == "cancelled" || transaction.status.state == "interrupted");
}

bool state_transition_allowed(std::string_view from, std::string_view to) {
    if (from == to)
        return true;
    if (from == "queued")
        return to == "running" || to == "cancelled" || to == "interrupted" || to == "failed";
    if (from == "running")
        return to == "succeeded" || to == "cancelled" || to == "interrupted" || to == "failed";
    return false;
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

fs::path reservation_root(const fs::path& root) {
    return root / "profile-reservations";
}

fs::path transaction_lock_root(const fs::path& root) {
    return root / "transaction-locks";
}

fs::path transaction_path(const fs::path& root, const std::string& operation_id) {
    return root / (operation_id + ".json");
}

fs::path transaction_lock_path(const fs::path& root, const std::string& operation_id) {
    return transaction_lock_root(root) / (operation_id + ".lock");
}

fs::path reservation_path(const fs::path& root, const std::string& profile_id) {
    return reservation_root(root) / (profile_id + ".reservation");
}

[[noreturn]] void throw_reservation_error(const std::string& operation, const fs::path& path, int error) {
    throw ValidationError(operation + " " + path.string() + ": " + std::strerror(error));
}

void write_all(int descriptor, const std::string& value, const fs::path& path) {
    const char* current = value.data();
    std::size_t remaining = value.size();
    while (remaining > 0) {
        const std::size_t count = std::min(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<ssize_t>::max())
        );
        const ssize_t written = ::write(descriptor, current, count);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            throw_reservation_error("cannot write profile reservation", path, errno);
        }
        if (written == 0)
            throw ValidationError("cannot write profile reservation " + path.string());
        current += written;
        remaining -= static_cast<std::size_t>(written);
    }
}

void fsync_file(int descriptor, const fs::path& path) {
    int result;
    do {
        result = ::fsync(descriptor);
    } while (result < 0 && errno == EINTR);
    if (result < 0)
        throw_reservation_error("cannot sync profile reservation", path, errno);
}

std::optional<std::string> read_reservation(const fs::path& path) {
    int descriptor;
    do {
        descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        if (errno == ENOENT)
            return std::nullopt;
        throw_reservation_error("cannot open profile reservation", path, errno);
    }
    platform::linux::OwnedFileDescriptor fd(descriptor);
    struct stat status {};
    if (::fstat(fd.get(), &status) < 0)
        throw_reservation_error("cannot inspect profile reservation", path, errno);
    if (!S_ISREG(status.st_mode) || status.st_size <= 0 || status.st_size > 64)
        throw ValidationError("invalid profile reservation " + path.string());
    std::string owner(static_cast<std::size_t>(status.st_size), '\0');
    std::size_t offset = 0;
    while (offset < owner.size()) {
        const ssize_t count = ::read(fd.get(), owner.data() + offset, owner.size() - offset);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            throw_reservation_error("cannot read profile reservation", path, errno);
        }
        if (count == 0)
            throw ValidationError("incomplete profile reservation " + path.string());
        offset += static_cast<std::size_t>(count);
    }
    validate_operation_id(owner);
    return owner;
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
        {"schemaVersion", 8},
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
        {"sourceSubvolume", transaction.source_subvolume},
        {"sourceFilesystemUuid", transaction.source_filesystem_uuid},
        {"sourceMountRoot", transaction.source_mount_root},
        {"localSnapshotDir", transaction.local_snapshot_dir},
        {"passphraseLabel", transaction.passphrase_label},
        {"createAutomaticKey", transaction.create_automatic_key},
        {"createdAt", transaction.created_at},
        {"updatedAt", transaction.updated_at},
        {"lastCompletedPhase", transaction.last_completed_phase},
        {"partitionTableBackup", transaction.partition_table_backup},
        {"partition", transaction.partition},
        {"partitionUuid", transaction.partition_uuid},
        {"luksUuid", transaction.luks_uuid},
        {"btrfsUuid", transaction.btrfs_uuid},
        {"mapper", transaction.mapper},
        {"inspectionMountPoint", transaction.inspection_mount_point},
        {"configurationState", transaction.configuration_state},
        {"credentialsState", transaction.credentials_state},
        {"profileReservationState", transaction.profile_reservation_state},
        {"cleanupResult", transaction.cleanup_result},
        {"cancelRequested", transaction.cancel_requested},
    };
}

DevicePreparationTransaction parse_transaction(const Json& value) {
    const int schema_version = value.value("schemaVersion", 0);
    if (!value.is_object() || schema_version != 8 || !value.contains("device"))
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
    result.source_filesystem_uuid = value.value("sourceFilesystemUuid", "");
    result.source_mount_root = value.value("sourceMountRoot", "");
    result.local_snapshot_dir = value.value("localSnapshotDir", "");
    result.passphrase_label = value.value("passphraseLabel", "");
    result.create_automatic_key = value.value("createAutomaticKey", true);
    result.created_at = value.value("createdAt", std::int64_t{0});
    result.updated_at = value.value("updatedAt", std::int64_t{0});
    result.last_completed_phase = value.value("lastCompletedPhase", "");
    result.partition_table_backup = value.value("partitionTableBackup", "");
    result.partition = value.value("partition", "");
    result.partition_uuid = value.value("partitionUuid", "");
    result.luks_uuid = value.value("luksUuid", "");
    result.btrfs_uuid = value.value("btrfsUuid", "");
    result.mapper = value.value("mapper", "");
    result.inspection_mount_point = value.value("inspectionMountPoint", "");
    result.configuration_state = value.value("configurationState", "not-started");
    result.credentials_state = value.value("credentialsState", "not-started");
    result.profile_reservation_state = value.value("profileReservationState", "not-held");
    result.cleanup_result = value.value("cleanupResult", "not-required");
    result.cancel_requested = value.value("cancelRequested", false);
    if (result.revision.value == 0 || result.status.operation_id.empty() || result.status.profile_id.empty() ||
        result.owner.bus_name.empty() || result.created_at <= 0 ||
        result.profile_name.empty() || result.source_subvolume.empty() || result.passphrase_label.empty() ||
        result.source_filesystem_uuid.empty() || result.source_mount_root.empty() ||
        result.local_snapshot_dir.empty())
        throw ValidationError("incomplete device preparation transaction");
    return result;
}

DevicePreparationTransaction read_transaction(const fs::path& path) {
    const state::document::BoundedDocumentReader reader;
    const Json document = Json::parse(
        reader.read(path, maximum_transaction_size, ::geteuid(), transaction_permissions)
    );
    return parse_transaction(document);
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

void DevicePreparationTransactionStore::save(DevicePreparationTransaction& transaction) const {
    if (transaction.status.operation_id.empty())
        throw ValidationError("device preparation transaction has no operation identifier");
    validate_operation_id(transaction.status.operation_id);
    prepare_root(root_);
    prepare_root(transaction_lock_root(root_));
    const TransactionLock lock(transaction_lock_path(root_, transaction.status.operation_id));
    const fs::path path = transaction_path(root_, transaction.status.operation_id);
    DevicePreparationTransaction changed = transaction;
    if (fs::exists(path)) {
        const auto current = read_transaction(path);
        if (current.status.operation_id != transaction.status.operation_id)
            throw ValidationError("transaction file name does not match its operation identifier");
        if (current.revision != transaction.revision)
            throw ValidationError("device preparation transaction revision conflict");
        if (terminal(current))
            throw ValidationError("terminal device preparation transaction is immutable");
        if (!state_transition_allowed(current.status.state, transaction.status.state))
            throw ValidationError("invalid device preparation transaction state transition");
        if (current.cancel_requested && !transaction.cancel_requested)
            throw ValidationError("device preparation cancellation request cannot be cleared");
        if (transaction.revision.value == std::numeric_limits<std::uint64_t>::max())
            throw ValidationError("device preparation transaction revision is exhausted");
        changed.revision.value += 1;
    } else {
        if (transaction.revision.value != 0)
            throw ValidationError("device preparation transaction is missing");
        changed.revision.value = 1;
    }
    platform::linux::filesystem::atomic_write(
        path,
        transaction_json(changed).dump(2) + "\n",
        transaction_permissions
    );
    transaction = std::move(changed);
}

DevicePreparationTransaction DevicePreparationTransactionStore::update(
    const std::string& operation_id,
    TransactionRevision expected_revision,
    const DevicePreparationTransition& transition
) const {
    validate_operation_id(operation_id);
    if (!transition)
        throw ValidationError("device preparation transaction transition is empty");
    prepare_root(root_);
    prepare_root(transaction_lock_root(root_));
    const TransactionLock lock(transaction_lock_path(root_, operation_id));
    const fs::path path = transaction_path(root_, operation_id);
    DevicePreparationTransaction current = read_transaction(path);
    if (current.status.operation_id != operation_id)
        throw ValidationError("transaction file name does not match its operation identifier");
    if (current.revision != expected_revision)
        throw ValidationError("device preparation transaction revision conflict");
    if (terminal(current))
        throw ValidationError("terminal device preparation transaction is immutable");
    DevicePreparationTransaction changed = current;
    transition(changed);
    if (changed.revision != current.revision || changed.status.operation_id != current.status.operation_id)
        throw ValidationError("device preparation transition changed immutable identity");
    if (!state_transition_allowed(current.status.state, changed.status.state))
        throw ValidationError("invalid device preparation transaction state transition");
    if (current.cancel_requested && !changed.cancel_requested)
        throw ValidationError("device preparation cancellation request cannot be cleared");
    if (changed.revision.value == std::numeric_limits<std::uint64_t>::max())
        throw ValidationError("device preparation transaction revision is exhausted");
    changed.revision.value += 1;
    platform::linux::filesystem::atomic_write(
        path,
        transaction_json(changed).dump(2) + "\n",
        transaction_permissions
    );
    return changed;
}

DevicePreparationTransaction DevicePreparationTransactionStore::update(
    const std::string& operation_id,
    const DevicePreparationTransition& transition
) const {
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto current = load(operation_id);
        try {
            return update(operation_id, current.revision, transition);
        } catch (const ValidationError& error) {
            if (std::string_view(error.what()) != "device preparation transaction revision conflict")
                throw;
        }
    }
    throw ValidationError("device preparation transaction remained busy");
}

DevicePreparationTransaction DevicePreparationTransactionStore::load(
    const std::string& operation_id
) const {
    validate_operation_id(operation_id);
    prepare_root(root_);
    const fs::path path = root_ / (operation_id + ".json");
    DevicePreparationTransaction transaction = read_transaction(path);
    if (transaction.status.operation_id != operation_id)
        throw ValidationError("transaction file name does not match its operation identifier");
    return transaction;
}

DevicePreparationTransactionScan DevicePreparationTransactionStore::load_and_prune() const {
    DevicePreparationTransactionScan scan;
    prepare_root(root_);
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(root_)) {
        if (entry.path().extension() != ".json")
            continue;
        try {
            DevicePreparationTransaction transaction = read_transaction(entry.path());
            if (entry.path().stem() != transaction.status.operation_id)
                throw ValidationError("transaction file name does not match its operation identifier");
            scan.transactions.push_back(std::move(transaction));
        } catch (const std::exception&) {
            const std::string operation_id = entry.path().stem().string();
            try {
                validate_operation_id(operation_id);
                scan.corrupted_operation_ids.push_back(operation_id);
            } catch (const std::exception&) {
            }
        }
    }

    const std::int64_t cutoff = now_seconds() - completed_ttl_.count();
    std::vector<DevicePreparationTransaction*> completed;
    for (auto& transaction : scan.transactions)
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
    std::erase_if(scan.transactions, [](const auto& transaction) { return transaction.status.operation_id.empty(); });
    return scan;
}

void DevicePreparationTransactionStore::reserve_profile(
    const std::string& profile_id,
    const std::string& operation_id
) const {
    validate_profile_id(profile_id);
    validate_operation_id(operation_id);
    prepare_root(root_);
    const fs::path directory = reservation_root(root_);
    prepare_root(directory);
    const fs::path path = reservation_path(root_, profile_id);

    int descriptor;
    do {
        descriptor = ::open(
            path.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
            transaction_permissions
        );
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        if (errno == EEXIST) {
            const auto owner = read_reservation(path);
            if (owner.has_value() && *owner == operation_id)
                return;
            throw ValidationError("profile is already reserved: " + profile_id);
        }
        throw_reservation_error("cannot create profile reservation", path, errno);
    }

    platform::linux::OwnedFileDescriptor fd(descriptor);
    try {
        write_all(fd.get(), operation_id, path);
        fsync_file(fd.get(), path);
        platform::linux::filesystem::fsync_dir(directory);
    } catch (...) {
        std::error_code ignored;
        fs::remove(path, ignored);
        throw;
    }
}

void DevicePreparationTransactionStore::release_profile(
    const std::string& profile_id,
    const std::string& operation_id
) const {
    validate_profile_id(profile_id);
    validate_operation_id(operation_id);
    prepare_root(root_);
    const fs::path directory = reservation_root(root_);
    prepare_root(directory);
    const fs::path path = reservation_path(root_, profile_id);
    const auto owner = read_reservation(path);
    if (!owner.has_value())
        return;
    if (*owner != operation_id)
        throw ValidationError("profile reservation is owned by another operation: " + profile_id);
    if (::unlink(path.c_str()) < 0) {
        if (errno == ENOENT)
            return;
        throw_reservation_error("cannot remove profile reservation", path, errno);
    }
    platform::linux::filesystem::fsync_dir(directory);
}

std::optional<std::string> DevicePreparationTransactionStore::profile_reservation_owner(
    const std::string& profile_id
) const {
    validate_profile_id(profile_id);
    prepare_root(root_);
    const fs::path directory = reservation_root(root_);
    prepare_root(directory);
    return read_reservation(reservation_path(root_, profile_id));
}

} // namespace btrfsbackup::daemon::control
