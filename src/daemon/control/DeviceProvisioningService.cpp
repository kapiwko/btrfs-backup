// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DeviceProvisioningService.hpp>

#include <sys/random.h>

#include <array>
#include <cerrno>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <core/Errors.hpp>
#include <core/ManagerProtocol.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

namespace btrfsbackup::daemon::control {
namespace {

std::string random_candidate_id() {
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
        throw ValidationError("cannot generate a device candidate identifier");
    }
    std::ostringstream value;
    value << "candidate-" << std::hex << std::setfill('0');
    for (const auto byte : bytes)
        value << std::setw(2) << static_cast<unsigned>(byte);
    return value.str();
}

bool complete_identity(const ProvisioningDevice& device) {
    return !device.path.empty() && device.size_bytes != 0 && !device.major_minor.empty() &&
        !device.sysfs_devpath.empty() && !device.transport.empty() && !device.device_graph.empty() &&
        (!device.wwn.empty() || !device.serial_id.empty() || !device.serial_short.empty());
}

} // namespace

DeviceProvisioningService::DeviceProvisioningService(
    IManagerAuthorizer& authorizer,
    IDeviceProvisioningBackend& backend,
    std::chrono::seconds candidate_lifetime,
    ProvisioningCandidateIdGenerator candidate_ids,
    ProvisioningCandidateClock clock,
    provisioning::StorageTopologyReader* topology_reader
) : authorizer_(authorizer), backend_(backend), candidate_lifetime_(candidate_lifetime),
    candidate_ids_(candidate_ids ? std::move(candidate_ids) : ProvisioningCandidateIdGenerator{random_candidate_id}),
    clock_(clock ? std::move(clock) : ProvisioningCandidateClock{[] { return std::chrono::steady_clock::now(); }}),
    topology_reader_(topology_reader) {
    if (candidate_lifetime_ <= std::chrono::seconds::zero())
        throw std::invalid_argument("provisioning candidate lifetime must be positive");
}

provisioning::StorageTopology DeviceProvisioningService::inspect_storage_topology(const std::string& caller) {
    authorize(caller, manager_protocol::method::inspect_storage_topology);
    if (topology_reader_ == nullptr)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InternalError, "storage topology is unavailable");
    provisioning::StorageTopology topology = topology_reader_->scan();
    std::vector<ProvisioningDevice> execution_devices = backend_.list_devices();
    const auto now = clock_();
    std::lock_guard lock(candidates_mutex_);
    expire_candidates(now);
    std::erase_if(candidates_, [&](const auto& item) { return item.second.caller == caller; });
    std::erase_if(topologies_, [&](const auto& item) { return item.second.caller == caller; });
    std::erase_if(plans_, [&](const auto& item) { return item.second.caller == caller; });
    std::set<std::string> allocated_ids;
    const auto allocate_id = [&] {
        for (int attempt = 0; attempt < 16; ++attempt) {
            std::string id = candidate_ids_();
            if (!id.empty() && !candidates_.contains(id) && !plans_.contains(id) && allocated_ids.insert(id).second)
                return id;
        }
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "cannot allocate a storage candidate identifier");
    };
    for (auto& device : topology.devices) {
        device.candidate_id = allocate_id();
        const auto execution = std::ranges::find(execution_devices, device.identity.major_minor, &ProvisioningDevice::major_minor);
        if (execution != execution_devices.end() && execution->path == device.identity.display_path &&
            execution->sysfs_devpath == device.identity.sysfs_path && execution->size_bytes == device.size_bytes &&
            execution->wwn == device.identity.wwn && execution->serial_id == device.identity.serial &&
            execution->serial_short == device.identity.serial_short && execution->transport == device.transport) {
            execution->candidate_id = device.candidate_id;
            candidates_.emplace(
                device.candidate_id,
                Candidate{*execution, caller, now + candidate_lifetime_}
            );
        }
        for (auto& region : device.regions) {
            std::visit(
                [&](auto& value) {
                    if constexpr (std::is_same_v<std::decay_t<decltype(value)>, provisioning::ExistingPartition>)
                        value.candidate_id = allocate_id();
                    else
                        value.id = allocate_id();
                },
                region
            );
        }
    }
    topologies_.insert_or_assign(
        caller,
        TopologySnapshot{topology, caller, now + candidate_lifetime_}
    );
    return topology;
}

provisioning::DevicePreparationPlan DeviceProvisioningService::build_device_preparation_plan(
    const std::string& caller,
    const provisioning::TopologyGeneration& expected_generation,
    const std::string& selected_candidate_id,
    provisioning::ProvisioningMode mode
) {
    authorize(caller, manager_protocol::method::build_device_preparation_plan);
    if (topology_reader_ == nullptr || expected_generation.empty() || selected_candidate_id.empty())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "storage topology is unavailable or expired");
    const provisioning::StorageTopology current = topology_reader_->scan();
    const auto now = clock_();
    std::lock_guard lock(candidates_mutex_);
    expire_candidates(now);
    const auto snapshot = topologies_.find(caller);
    if (snapshot == topologies_.end() || snapshot->second.topology.generation != expected_generation)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "storage topology is unavailable or expired");
    if (current.generation != expected_generation)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "storage topology changed");
    std::string plan_id;
    for (int attempt = 0; attempt < 16 && plan_id.empty(); ++attempt) {
        std::string candidate = candidate_ids_();
        if (!candidate.empty() && !plans_.contains(candidate) && !candidates_.contains(candidate))
            plan_id = std::move(candidate);
    }
    if (plan_id.empty())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "cannot allocate a preparation plan identifier");
    auto plan = plan_builder_.build(
        snapshot->second.topology,
        expected_generation,
        selected_candidate_id,
        mode,
        plan_id
    );
    plans_.insert_or_assign(plan_id, StoredPlan{plan, caller, now + candidate_lifetime_});
    return plan;
}

std::vector<ProvisioningDevice> DeviceProvisioningService::list_devices(const std::string& caller) {
    authorize(caller, manager_protocol::method::list_provisioning_devices);
    std::vector<ProvisioningDevice> devices = backend_.list_devices();
    const auto now = clock_();
    std::lock_guard lock(candidates_mutex_);
    expire_candidates(now);
    std::erase_if(candidates_, [&](const auto& item) { return item.second.caller == caller; });
    std::erase_if(devices, [](const auto& device) { return !complete_identity(device); });
    for (auto& device : devices) {
        bool inserted = false;
        for (int attempt = 0; attempt < 16 && !inserted; ++attempt) {
            device.candidate_id = candidate_ids_();
            if (device.candidate_id.empty())
                continue;
            inserted = candidates_.emplace(
                                      device.candidate_id,
                                      Candidate{device, caller, now + candidate_lifetime_}
            )
                           .second;
        }
        if (!inserted)
            throw dbus::ManagerOperationError(
                dbus::ManagerErrorCode::Conflict,
                "cannot allocate a device candidate identifier"
            );
    }
    return devices;
}
std::vector<std::string> DeviceProvisioningService::list_source_candidates(const std::string& caller) {
    authorize(caller, manager_protocol::method::list_source_candidates);
    return backend_.list_source_candidates();
}

void DeviceProvisioningService::authorize(const std::string& caller, std::string_view method) const {
    const auto action = manager_method_authorization_action(method);
    if (caller.empty() || !action.has_value() || !authorizer_.authorize(caller, *action) ||
        !authorizer_.caller_is_active(caller))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotAuthorized, "operation is not authorized");
}

void DeviceProvisioningService::authorize_owner_or_admin(
    const std::string& caller,
    std::uint32_t caller_uid,
    const std::string& operation_id,
    std::string_view method
) const {
    const bool caller_owns_operation = backend_.owned_by(
        operation_id,
        {.bus_name = caller, .uid = caller_uid}
    );
    if (caller_owns_operation && !caller.empty() && authorizer_.caller_is_active(caller))
        return;
    authorize(caller, method);
}

void DeviceProvisioningService::expire_candidates(std::chrono::steady_clock::time_point now) {
    std::erase_if(candidates_, [&](const auto& item) { return item.second.expires_at <= now; });
    std::erase_if(topologies_, [&](const auto& item) { return item.second.expires_at <= now; });
    std::erase_if(plans_, [&](const auto& item) { return item.second.expires_at <= now; });
}

ProvisioningDevice DeviceProvisioningService::take_candidate(
    const std::string& caller,
    const std::string& candidate_id
) {
    std::lock_guard lock(candidates_mutex_);
    expire_candidates(clock_());
    const auto candidate = candidates_.find(candidate_id);
    if (candidate == candidates_.end() || candidate->second.caller != caller)
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::NotFound,
            "device candidate is unavailable or expired"
        );
    ProvisioningDevice device = std::move(candidate->second.device);
    candidates_.erase(candidate);
    return device;
}

ProvisioningDevice DeviceProvisioningService::find_candidate(
    const std::string& caller,
    const std::string& candidate_id
) {
    std::lock_guard lock(candidates_mutex_);
    expire_candidates(clock_());
    const auto candidate = candidates_.find(candidate_id);
    if (candidate == candidates_.end() || candidate->second.caller != caller)
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::NotFound,
            "device candidate is unavailable or expired"
        );
    return candidate->second.device;
}

provisioning::DevicePreparationPlan DeviceProvisioningService::find_plan(
    const std::string& caller,
    const std::string& plan_id
) {
    const auto now = clock_();
    std::lock_guard lock(candidates_mutex_);
    expire_candidates(now);
    const auto plan = plans_.find(plan_id);
    if (plan == plans_.end() || plan->second.caller != caller)
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::NotFound,
            "device preparation plan is unavailable or expired"
        );
    return plan->second.plan;
}

provisioning::DevicePreparationPlan DeviceProvisioningService::take_plan(
    const std::string& caller,
    const std::string& plan_id
) {
    const auto now = clock_();
    std::lock_guard lock(candidates_mutex_);
    expire_candidates(now);
    const auto plan = plans_.find(plan_id);
    if (plan == plans_.end() || plan->second.caller != caller)
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::NotFound,
            "device preparation plan is unavailable or expired"
        );
    auto result = plan->second.plan;
    plans_.erase(plan);
    return result;
}

provisioning::StorageTopology DeviceProvisioningService::find_topology(
    const std::string& caller,
    const provisioning::TopologyGeneration& generation
) {
    const auto now = clock_();
    std::lock_guard lock(candidates_mutex_);
    expire_candidates(now);
    const auto topology = topologies_.find(caller);
    if (topology == topologies_.end() || topology->second.topology.generation != generation)
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::NotFound,
            "storage topology is unavailable or expired"
        );
    return topology->second.topology;
}

DevicePreparationStatus DeviceProvisioningService::start(
    const std::string& caller,
    std::uint32_t caller_uid,
    const DevicePreparationRequest& request,
    int passphrase_fd
) {
    if (request.profile_id.empty() || request.profile_name.empty() ||
        (request.plan_id.empty() && request.candidate_id.empty()) ||
        request.source_subvolume.empty() || request.passphrase_label.empty())
        throw ValidationError("device preparation request is incomplete");
    static_cast<void>(ProfileId{request.profile_id});
    const std::filesystem::path source(request.source_subvolume);
    if (!source.is_absolute() || source.lexically_normal() != source)
        throw ValidationError("device preparation source path is invalid");
    if (passphrase_fd < 0)
        throw ValidationError("device preparation passphrase descriptor is invalid");
    if (request.profile_name.size() > 120 || request.passphrase_label.size() > 80)
        throw ValidationError("device preparation text is too long");
    std::string candidate_id = request.candidate_id;
    if (!request.plan_id.empty()) {
        if (topology_reader_ == nullptr)
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "storage topology is unavailable");
        const auto plan = find_plan(caller, request.plan_id);
        if (plan.mode != provisioning::ProvisioningMode::EraseWholeDevice)
            throw dbus::ManagerOperationError(
                dbus::ManagerErrorCode::Conflict,
                "device preparation plan mode is not executable yet"
            );
        const auto expected = find_topology(caller, plan.topology_generation);
        const auto current = topology_reader_->scan();
        const auto blockers = storage_safety_inspector_.inspect(expected, current, plan);
        if (!blockers.empty())
            throw dbus::ManagerOperationError(
                dbus::ManagerErrorCode::Conflict,
                "storage safety check failed: " + blockers.front().code
            );
        candidate_id = plan.device_id;
    }
    const ProvisioningDevice candidate = find_candidate(caller, candidate_id);
    const std::vector<std::string> safety_reasons = backend_.inspect_safety(candidate);
    if (!safety_reasons.empty())
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Conflict,
            "selected device is not safe for destructive preparation: " + safety_reasons.front()
        );
    authorize(caller, manager_protocol::method::start_device_preparation);
    if (!request.plan_id.empty()) {
        const auto consumed_plan = take_plan(caller, request.plan_id);
        if (consumed_plan.device_id != candidate_id)
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "device preparation plan changed");
    }
    const ProvisioningDevice expected_device = take_candidate(caller, candidate_id);
    return backend_.start(
        request,
        expected_device,
        {.bus_name = caller, .uid = caller_uid},
        passphrase_fd
    );
}

DevicePreparationStatus DeviceProvisioningService::status(
    const std::string& caller,
    std::uint32_t caller_uid,
    const std::string& operation_id
) const {
    if (operation_id.empty())
        throw ValidationError("operation identifier is empty");
    authorize_owner_or_admin(caller, caller_uid, operation_id, manager_protocol::method::get_device_preparation);
    return backend_.status(operation_id);
}

void DeviceProvisioningService::cancel(
    const std::string& caller,
    std::uint32_t caller_uid,
    const std::string& operation_id
) {
    if (operation_id.empty())
        throw ValidationError("operation identifier is empty");
    authorize_owner_or_admin(caller, caller_uid, operation_id, manager_protocol::method::cancel_device_preparation);
    backend_.cancel(operation_id);
}

} // namespace btrfsbackup::daemon::control
