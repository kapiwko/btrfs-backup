// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemDeviceProvisioningBackend.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <sys/random.h>
#include <thread>
#include <unordered_set>

#include <core/Errors.hpp>
#include <daemon/control/DestructiveDeviceSafetyInspector.hpp>
#include <daemon/control/DevicePreparationExecutor.hpp>
#include <daemon/control/DevicePreparationTransaction.hpp>
#include <daemon/control/DevicePreparationUnitController.hpp>
#include <daemon/control/ExistingTargetInspector.hpp>
#include <daemon/control/ProvisioningDeviceEnumerator.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <platform/linux/filesystem/SecretFile.hpp>
#include <platform/linux/filesystem/TrustedDirectory.hpp>
#include <platform/linux/storage/BlockDeviceMetadata.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>
#include <platform/linux/storage/MountInfo.hpp>
#include <platform/linux/storage/PartitionTableOperations.hpp>
#include <platform/linux/storage/SignatureOperations.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

using platform::linux::OwnedFileDescriptor;

std::string next_operation_id() {
    std::array<unsigned char, 16> bytes{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        throw ValidationError("cannot generate a device preparation operation identifier");
    }
    std::ostringstream value;
    value << "prepare-" << std::hex << std::setfill('0');
    for (const auto byte : bytes)
        value << std::setw(2) << static_cast<unsigned>(byte);
    return value.str();
}

std::int64_t system_time_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    )
        .count();
}

void validate_execution_target(const DevicePreparationTarget& target) {
    const auto& identity = target.device.identity;
    const bool whole_device = target.mode == provisioning::ProvisioningMode::EraseWholeDevice;
    const bool existing_partition = target.mode == provisioning::ProvisioningMode::ReformatExistingPartition;
    const bool adoption = target.mode == provisioning::ProvisioningMode::AdoptExistingTarget;
    const bool free_space = target.mode == provisioning::ProvisioningMode::CreatePartitionInUnallocatedSpace;
    if ((!whole_device && !existing_partition && !adoption && !free_space) ||
        (whole_device && target.partition.has_value()) ||
        ((existing_partition || adoption) && !target.partition.has_value()) ||
        (free_space &&
         (target.partition.has_value() || !target.free_region.has_value() ||
          !target.planned_partition_geometry.has_value())) ||
        (adoption && !target.expected_inspection.has_value()))
        throw ValidationError("device preparation target is incomplete");
    if (identity.display_path.empty() || identity.major_minor.empty() || identity.sysfs_path.empty() ||
        identity.size_bytes == 0 || target.device.logical_sector_size == 0 || target.device.transport.empty() ||
        (identity.wwn.empty() && identity.serial.empty() && identity.serial_short.empty()))
        throw ValidationError("device preparation target identity is incomplete");
    if (existing_partition) {
        const auto& partition = *target.partition;
        if (partition.identity.display_path.empty() || partition.identity.major_minor.empty() ||
            partition.identity.sysfs_path.empty() || partition.identity.size_bytes == 0 ||
            partition.partition_number == 0 || partition.sector_count == 0)
            throw ValidationError("partition preparation target identity is incomplete");
    }
    if (free_space &&
        (target.device.partition_table.type != provisioning::PartitionTableType::Gpt ||
         target.device.partition_table.identifier.empty() || target.free_region->sector_count == 0 ||
         target.planned_partition_geometry->sector_count == 0 ||
         target.planned_partition_geometry->partition_number == 0))
        throw ValidationError("free-space preparation target identity is incomplete");
    if (adoption &&
        (target.expected_inspection->luks_uuid.empty() || target.expected_inspection->btrfs_uuid.empty() ||
         target.expected_inspection->partition_uuid.empty() || target.expected_inspection->repository_id.empty()))
        throw ValidationError("existing target adoption fingerprint is incomplete");
}

} // namespace

struct SystemDeviceProvisioningBackend::State {
    mutable std::mutex mutex;
    DevicePreparationTransaction transaction;
    bool restored = false;
};

struct SystemDeviceProvisioningBackend::Impl {
    fs::path mountinfo_path;
    IDestructiveDeviceSafetyInspector& safety_inspector;
    IDevicePreparationUnitController& units;
    DevicePreparationTransactionStore transactions;
    ProvisioningDeviceEnumerator devices;
    platform::linux::storage::IPartitionTableOperations& partition_tables;
    DevicePreparationExecutor executor;
    IExistingTargetInspector* existing_target_inspector;
    fs::path inspection_mount_root;
    mutable std::mutex jobs_mutex;
    std::map<std::string, std::shared_ptr<State>> jobs;
    std::condition_variable_any cleanup_wakeup;
    std::jthread cleanup_worker;

    Impl(
        CredentialAdministrationRoots roots,
        fs::path target_mount_root,
        fs::path mountinfo,
        fs::path transaction_root,
        provisioning::StorageTopologyReader& topology,
        backup::ICommandRunner& commands,
        platform::linux::storage::ISignatureOperations& signatures,
        platform::linux::storage::IBlockDeviceMetadataReader& metadata,
        platform::linux::storage::IPartitionTableOperations& partition_tables,
        platform::linux::storage::ICryptsetupOperations& cryptsetup,
        backup::IBtrfsOperations& btrfs,
        config::IConfigurationActivator& configuration_activator,
        ICredentialAdministrationBackend& credentials,
        IDestructiveDeviceSafetyInspector& device_safety_inspector,
        IDevicePreparationUnitController& unit_controller,
        bool recover_existing,
        IExistingTargetInspector* target_inspector,
        fs::path target_inspection_root
    )
        : mountinfo_path(std::move(mountinfo)),
          safety_inspector(device_safety_inspector),
          units(unit_controller),
          transactions(std::move(transaction_root)),
          devices(topology),
          partition_tables(partition_tables),
          executor(
              std::move(roots),
              std::move(target_mount_root),
              commands,
              signatures,
              metadata,
              partition_tables,
              cryptsetup,
              btrfs,
              configuration_activator,
              credentials,
              device_safety_inspector,
              transactions,
              devices,
              target_inspector,
              target_inspection_root
          ),
          existing_target_inspector(target_inspector),
          inspection_mount_root(std::move(target_inspection_root)) {
        if (existing_target_inspector != nullptr && inspection_mount_root.empty())
            throw ValidationError("existing target inspection mount root is empty");
        restore_transactions(recover_existing);
        cleanup_worker = std::jthread([this](std::stop_token stop) {
            std::mutex wait_mutex;
            std::unique_lock wait_lock(wait_mutex);
            while (!stop.stop_requested()) {
                cleanup_wakeup.wait_for(wait_lock, stop, std::chrono::hours(1), [] { return false; });
                if (!stop.stop_requested()) {
                    try {
                        prune_completed();
                    } catch (const std::exception& error) {
                        std::cerr << "Cannot prune device preparation transactions: " << error.what() << '\n';
                    }
                }
            }
        });
    }

    void restore_transactions(bool recover_existing) {
        for (auto transaction : transactions.load_and_prune()) {
            if (recover_existing &&
                (transaction.status.state == "queued" || transaction.status.state == "running") &&
                !units.active(transaction.status.operation_id)) {
                transaction.status.state = "interrupted";
                transaction.status.error_code = "device-preparation.daemon-restarted";
                transaction.status.recovery_action =
                    "Inspect the recorded device and lastCompletedPhase; complete or remove partial structures manually.";
                transaction.status.can_cancel = false;
                transaction.cleanup_result = "not-required";
                transaction.updated_at = system_time_seconds();
                transactions.save(transaction);
                if (!transaction.mapper.empty()) {
                    try {
                        units.recover(transaction.status.operation_id);
                    } catch (const std::exception& error) {
                        std::cerr << "Cannot start device preparation cleanup: " << error.what() << '\n';
                    }
                }
            }
            auto state = std::make_shared<State>();
            state->transaction = std::move(transaction);
            state->restored = true;
            const auto operation_id = state->transaction.status.operation_id;
            jobs.emplace(operation_id, std::move(state));
        }
    }

    void prune_completed() {
        const auto retained_transactions = transactions.load_and_prune();
        std::unordered_set<std::string> retained;
        for (const auto& transaction : retained_transactions)
            retained.insert(transaction.status.operation_id);
        std::lock_guard lock(jobs_mutex);
        for (const auto& transaction : retained_transactions) {
            const auto item = jobs.find(transaction.status.operation_id);
            if (item == jobs.end()) {
                auto state = std::make_shared<State>();
                state->transaction = transaction;
                state->restored = true;
                jobs.emplace(transaction.status.operation_id, std::move(state));
            } else {
                std::lock_guard state_lock(item->second->mutex);
                item->second->transaction = transaction;
            }
        }
        std::erase_if(jobs, [&](const auto& item) { return !retained.contains(item.first); });
    }

    DevicePreparationTransaction load_current(const std::string& operation_id) const {
        DevicePreparationTransaction transaction = transactions.load(operation_id);
        if ((transaction.status.state == "queued" || transaction.status.state == "running") &&
            !units.active(operation_id)) {
            transaction.status.state = "interrupted";
            transaction.status.error_code = "device-preparation.helper-exited";
            transaction.status.recovery_action =
                "Inspect the recorded device and lastCompletedPhase; complete or remove partial structures manually.";
            transaction.status.can_cancel = false;
            transaction.updated_at = system_time_seconds();
            transactions.save(transaction);
            if (!transaction.mapper.empty()) {
                try {
                    units.recover(operation_id);
                } catch (const std::exception& error) {
                    std::cerr << "Cannot start device preparation cleanup: " << error.what() << '\n';
                }
            }
        }
        return transaction;
    }

    void register_job(const std::shared_ptr<State>& state) {
        constexpr std::size_t maximum_active_jobs = 4;
        std::size_t active = 0;
        std::lock_guard lock(jobs_mutex);
        for (const auto& [operation_id, current] : jobs) {
            static_cast<void>(operation_id);
            std::lock_guard state_lock(current->mutex);
            if (current->transaction.status.state == "queued" || current->transaction.status.state == "running")
                ++active;
        }
        if (active >= maximum_active_jobs)
            throw dbus::ManagerOperationError(
                dbus::ManagerErrorCode::Busy,
                "too many device preparation operations are active"
            );
        if (jobs.contains(state->transaction.status.operation_id))
            throw dbus::ManagerOperationError(
                dbus::ManagerErrorCode::Conflict,
                "device preparation operation identifier collision"
            );
        transactions.save(state->transaction);
        jobs.emplace(state->transaction.status.operation_id, state);
    }

    template <typename Mutator>
    void update(const std::shared_ptr<State>& state, Mutator mutator) {
        std::lock_guard lock(state->mutex);
        mutator(state->transaction);
        state->transaction.updated_at = system_time_seconds();
        transactions.save(state->transaction);
    }
};

SystemDeviceProvisioningBackend::SystemDeviceProvisioningBackend(
    CredentialAdministrationRoots roots,
    fs::path target_mount_root,
    fs::path mountinfo_path,
    fs::path transaction_root,
    provisioning::StorageTopologyReader& topology,
    backup::ICommandRunner& commands,
    platform::linux::storage::ISignatureOperations& signatures,
    platform::linux::storage::IBlockDeviceMetadataReader& metadata,
    platform::linux::storage::IPartitionTableOperations& partition_tables,
    platform::linux::storage::ICryptsetupOperations& cryptsetup,
    backup::IBtrfsOperations& btrfs,
    config::IConfigurationActivator& configuration_activator,
    ICredentialAdministrationBackend& credentials,
    IDestructiveDeviceSafetyInspector& safety_inspector,
    IDevicePreparationUnitController& units,
    bool recover_existing,
    IExistingTargetInspector* existing_target_inspector,
    fs::path inspection_mount_root
)
    : impl_(std::make_unique<Impl>(std::move(roots), std::move(target_mount_root), std::move(mountinfo_path), std::move(transaction_root), topology, commands, signatures, metadata, partition_tables, cryptsetup, btrfs, configuration_activator, credentials, safety_inspector, units, recover_existing, existing_target_inspector, std::move(inspection_mount_root))) {
}

SystemDeviceProvisioningBackend::~SystemDeviceProvisioningBackend() noexcept = default;

std::vector<std::string> SystemDeviceProvisioningBackend::list_source_candidates() {
    const auto paths = platform::linux::storage::btrfs_mount_targets(impl_->mountinfo_path);
    std::vector<std::string> result;
    result.reserve(paths.size());
    for (const auto& path : paths)
        result.push_back(path);
    return result;
}

std::vector<std::string> SystemDeviceProvisioningBackend::inspect_safety(
    const DevicePreparationTarget& target
) const {
    return impl_->safety_inspector.inspect(provisioning_device_snapshot(target.device), target);
}

provisioning::PlannedPartitionGeometry SystemDeviceProvisioningBackend::plan_partition_geometry(
    const provisioning::StorageDevice& device,
    const provisioning::UnallocatedRegion& free_region
) const {
    const auto geometry = impl_->partition_tables.plan_partition_in_free_space(
        device.identity.display_path,
        device.identity.major_minor,
        device.partition_table.identifier,
        device.logical_sector_size,
        free_region.start_sector,
        free_region.sector_count
    );
    return {
        .start_sector = geometry.start_sector,
        .sector_count = geometry.sector_count,
        .partition_number = geometry.partition_number,
    };
}

provisioning::ExistingTargetInspectionSummary SystemDeviceProvisioningBackend::inspect_existing_target(
    const DevicePreparationTarget& target,
    int credential_fd
) {
    if (impl_->existing_target_inspector == nullptr)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InternalError, "existing target inspection is unavailable");
    if (target.mode != provisioning::ProvisioningMode::AdoptExistingTarget || !target.partition.has_value())
        throw ValidationError("existing target inspection requires a partition candidate");
    platform::linux::filesystem::ensure_trusted_directory(impl_->inspection_mount_root, 0700);
    const std::string session_id = next_operation_id();
    const fs::path mount_point = impl_->inspection_mount_root / session_id;
    platform::linux::filesystem::ensure_trusted_directory(mount_point, 0700, impl_->inspection_mount_root);
    try {
        auto result = impl_->existing_target_inspector->inspect(
            *target.partition,
            session_id,
            mount_point,
            credential_fd
        );
        std::error_code cleanup_error;
        if (!fs::remove(mount_point, cleanup_error) || cleanup_error)
            throw ValidationError("cannot remove existing target inspection mount point");
        return result;
    } catch (...) {
        std::error_code cleanup_error;
        static_cast<void>(fs::remove(mount_point, cleanup_error));
        throw;
    }
}

DevicePreparationStatus SystemDeviceProvisioningBackend::start(
    const DevicePreparationRequest& request,
    const DevicePreparationTarget& target,
    const DevicePreparationOwner& owner,
    int passphrase_fd
) {
    impl_->prune_completed();
    validate_execution_target(target);
    const ProvisioningDevice expected_device = provisioning_device_snapshot(target.device);
    OwnedFileDescriptor secret = platform::linux::filesystem::copy_secret_to_sealed_file(passphrase_fd);
    auto state = std::make_shared<State>();
    const std::int64_t now = system_time_seconds();
    state->transaction = {
        .status = {
            .operation_id = next_operation_id(),
            .profile_id = request.profile_id,
            .state = "queued",
            .phase = "inspect",
            .error_code = {},
            .recovery_action = {},
            .can_cancel = true,
        },
        .owner = owner,
        .device = expected_device,
        .target = target,
        .profile_name = request.profile_name,
        .source_subvolume = request.source_subvolume,
        .passphrase_label = request.passphrase_label,
        .create_automatic_key = target.mode == provisioning::ProvisioningMode::AdoptExistingTarget ? false : request.create_automatic_key,
        .created_at = now,
        .updated_at = now,
        .last_completed_phase = {},
        .partition = target.partition.has_value() ? target.partition->identity.display_path : std::string{},
        .partition_uuid = {},
        .luks_uuid = {},
        .btrfs_uuid = {},
        .mapper = {},
        .inspection_mount_point = {},
        .configuration_state = "not-started",
        .credentials_state = "not-started",
        .cleanup_result = "not-required",
    };
    impl_->register_job(state);
    try {
        impl_->units.start(state->transaction.status.operation_id, secret.get());
    } catch (...) {
        impl_->update(state, [](auto& transaction) {
            transaction.status.state = "failed";
            transaction.status.error_code = "device-preparation.helper-start-failed";
            transaction.status.recovery_action = "Retry device preparation after checking the helper service.";
            transaction.status.can_cancel = false;
        });
        throw;
    }
    return status(state->transaction.status.operation_id);
}

DevicePreparationStatus SystemDeviceProvisioningBackend::status(const std::string& operation_id) const {
    try {
        return impl_->load_current(operation_id).status;
    } catch (const std::exception&) {
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::NotFound,
            "device preparation operation not found"
        );
    }
}

bool SystemDeviceProvisioningBackend::owned_by(
    const std::string& operation_id,
    const DevicePreparationOwner& owner
) const {
    try {
        const DevicePreparationTransaction transaction = impl_->load_current(operation_id);
        bool restored = false;
        {
            std::lock_guard lock(impl_->jobs_mutex);
            const auto item = impl_->jobs.find(operation_id);
            restored = item != impl_->jobs.end() && item->second->restored;
        }
        return !owner.bus_name.empty() && transaction.owner.uid == owner.uid &&
            (transaction.owner.bus_name == owner.bus_name || restored);
    } catch (const std::exception&) {
        return false;
    }
}

void SystemDeviceProvisioningBackend::cancel(const std::string& operation_id) {
    DevicePreparationTransaction transaction;
    try {
        transaction = impl_->load_current(operation_id);
    } catch (const std::exception&) {
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::NotFound,
            "device preparation operation not found"
        );
    }
    if (!transaction.status.can_cancel)
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Conflict,
            "device preparation can no longer be cancelled"
        );
    impl_->units.stop(operation_id);
    transaction.status.state = "cancelled";
    transaction.status.phase = "cancelled";
    transaction.status.can_cancel = false;
    transaction.updated_at = system_time_seconds();
    impl_->transactions.save(transaction);
}

void SystemDeviceProvisioningBackend::execute_operation(
    const std::string& operation_id,
    int passphrase_fd
) {
    impl_->executor.execute(operation_id, passphrase_fd);
}

void SystemDeviceProvisioningBackend::recover_operation(const std::string& operation_id) {
    impl_->executor.recover(operation_id);
}

} // namespace btrfsbackup::daemon::control
