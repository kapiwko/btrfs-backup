// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DeviceProvisioningService.hpp>

#include <sys/random.h>

#include <array>
#include <algorithm>
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

DevicePreparationTarget planned_target(
    const provisioning::StorageTopology& topology,
    const provisioning::DevicePreparationPlan& plan
) {
    const auto device = std::ranges::find(topology.devices, plan.device_id, &provisioning::StorageDevice::candidate_id);
    if (device == topology.devices.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "planned device snapshot is missing");
    DevicePreparationTarget result{.mode = plan.mode, .device = *device, .partition = std::nullopt};
    if (plan.partition_id.has_value()) {
        for (const auto& region : device->regions) {
            const auto* partition = std::get_if<provisioning::ExistingPartition>(&region);
            if (partition != nullptr && partition->candidate_id == *plan.partition_id) {
                result.partition = *partition;
                break;
            }
        }
        if (!result.partition.has_value())
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "planned partition snapshot is missing");
    }
    return result;
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
    const auto now = clock_();
    std::lock_guard lock(candidates_mutex_);
    expire_candidates(now);
    std::erase_if(topologies_, [&](const auto& item) { return item.second.caller == caller; });
    std::erase_if(plans_, [&](const auto& item) { return item.second.caller == caller; });
    std::erase_if(inspections_, [&](const auto& item) { return item.second.caller == caller; });
    std::set<std::string> allocated_ids;
    const auto allocate_id = [&] {
        for (int attempt = 0; attempt < 16; ++attempt) {
            std::string id = candidate_ids_();
            if (!id.empty() && !plans_.contains(id) && !inspections_.contains(id) && allocated_ids.insert(id).second)
                return id;
        }
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "cannot allocate a storage candidate identifier");
    };
    for (auto& device : topology.devices) {
        device.candidate_id = allocate_id();
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

provisioning::ExistingTargetInspection DeviceProvisioningService::inspect_existing_target(
    const std::string& caller,
    const provisioning::TopologyGeneration& expected_generation,
    const provisioning::PartitionCandidateId& partition_id,
    int credential_fd
) {
    if (expected_generation.empty() || partition_id.empty() || credential_fd < 0)
        throw ValidationError("existing target inspection request is incomplete");
    authorize(caller, manager_protocol::method::inspect_existing_target);
    if (topology_reader_ == nullptr)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "storage topology is unavailable");
    const auto expected = find_topology(caller, expected_generation);
    provisioning::DevicePreparationPlan validation_plan;
    validation_plan.topology_generation = expected_generation;
    validation_plan.mode = provisioning::ProvisioningMode::AdoptExistingTarget;
    validation_plan.partition_id = partition_id;
    validation_plan.destructive_scope.kind = provisioning::DestructiveScopeKind::ExistingPartition;
    validation_plan.destructive_scope.partition_id = partition_id;
    for (const auto& device : expected.devices) {
        const bool found = std::ranges::any_of(device.regions, [&](const auto& region) {
            const auto* partition = std::get_if<provisioning::ExistingPartition>(&region);
            return partition != nullptr && partition->candidate_id == partition_id;
        });
        if (found) {
            validation_plan.device_id = device.candidate_id;
            validation_plan.destructive_scope.device_id = device.candidate_id;
            break;
        }
    }
    if (validation_plan.device_id.empty())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "storage partition candidate is unavailable");
    const DevicePreparationTarget target = planned_target(expected, validation_plan);
    if (!target.partition->suitable_for_adoption)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "storage partition cannot be adopted");
    const auto inspect_safety = [&](const provisioning::StorageTopology& current) {
        const auto blockers = storage_safety_inspector_.inspect(expected, current, validation_plan);
        if (!blockers.empty())
            throw dbus::ManagerOperationError(
                dbus::ManagerErrorCode::Conflict,
                "existing target safety check failed: " + blockers.front().code
            );
    };
    inspect_safety(topology_reader_->scan());
    const auto summary = backend_.inspect_existing_target(target, credential_fd);
    inspect_safety(topology_reader_->scan());

    const auto now = clock_();
    std::lock_guard lock(candidates_mutex_);
    expire_candidates(now);
    const auto snapshot = topologies_.find(caller);
    if (snapshot == topologies_.end() || snapshot->second.topology.generation != expected_generation)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "storage topology is unavailable or expired");
    std::string inspection_id;
    for (int attempt = 0; attempt < 16 && inspection_id.empty(); ++attempt) {
        std::string candidate = candidate_ids_();
        if (!candidate.empty() && !inspections_.contains(candidate) && !plans_.contains(candidate))
            inspection_id = std::move(candidate);
    }
    if (inspection_id.empty())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "cannot allocate a target inspection identifier");
    provisioning::ExistingTargetInspection inspection{
        .inspection_id = inspection_id,
        .topology_generation = expected_generation,
        .device_id = validation_plan.device_id,
        .partition_id = partition_id,
        .target = summary,
    };
    inspections_.insert_or_assign(
        inspection_id,
        StoredInspection{inspection, caller, now + candidate_lifetime_}
    );
    return inspection;
}

provisioning::DevicePreparationPlan DeviceProvisioningService::build_device_preparation_plan(
    const std::string& caller,
    const provisioning::TopologyGeneration& expected_generation,
    const std::string& selected_candidate_id,
    provisioning::ProvisioningMode mode,
    const std::string& inspection_id
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
    std::optional<std::string> validated_inspection_id;
    if (mode == provisioning::ProvisioningMode::AdoptExistingTarget) {
        const auto inspection = inspections_.find(inspection_id);
        if (inspection == inspections_.end() || inspection->second.caller != caller ||
            inspection->second.inspection.topology_generation != expected_generation ||
            inspection->second.inspection.partition_id != selected_candidate_id)
            throw dbus::ManagerOperationError(
                dbus::ManagerErrorCode::NotFound,
                "existing target inspection is unavailable or expired"
            );
        validated_inspection_id = inspection_id;
    }
    std::string plan_id;
    for (int attempt = 0; attempt < 16 && plan_id.empty(); ++attempt) {
        std::string candidate = candidate_ids_();
        if (!candidate.empty() && !plans_.contains(candidate) && !inspections_.contains(candidate))
            plan_id = std::move(candidate);
    }
    if (plan_id.empty())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "cannot allocate a preparation plan identifier");
    auto plan = plan_builder_.build(
        snapshot->second.topology,
        expected_generation,
        selected_candidate_id,
        mode,
        plan_id,
        std::move(validated_inspection_id)
    );
    plans_.insert_or_assign(plan_id, StoredPlan{plan, caller, now + candidate_lifetime_});
    return plan;
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
    std::erase_if(topologies_, [&](const auto& item) { return item.second.expires_at <= now; });
    std::erase_if(plans_, [&](const auto& item) { return item.second.expires_at <= now; });
    std::erase_if(inspections_, [&](const auto& item) { return item.second.expires_at <= now; });
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
    if (request.profile_id.empty() || request.profile_name.empty() || request.plan_id.empty() ||
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
    if (topology_reader_ == nullptr)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "storage topology is unavailable");
    const auto plan = find_plan(caller, request.plan_id);
    if (plan.mode != provisioning::ProvisioningMode::EraseWholeDevice &&
        plan.mode != provisioning::ProvisioningMode::ReformatExistingPartition)
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Conflict,
            "device preparation plan mode is not executable yet"
        );
    const auto expected = find_topology(caller, plan.topology_generation);
    const DevicePreparationTarget target = planned_target(expected, plan);
    const auto current = topology_reader_->scan();
    const auto blockers = storage_safety_inspector_.inspect(expected, current, plan);
    if (!blockers.empty())
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Conflict,
            "storage safety check failed: " + blockers.front().code
        );
    const std::vector<std::string> safety_reasons = backend_.inspect_safety(target);
    if (!safety_reasons.empty())
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Conflict,
            "selected device is not safe for destructive preparation: " + safety_reasons.front()
        );
    authorize(caller, manager_protocol::method::start_device_preparation);
    const auto consumed_plan = take_plan(caller, request.plan_id);
    if (consumed_plan.id != plan.id || consumed_plan.device_id != plan.device_id)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "device preparation plan changed");
    return backend_.start(
        request,
        target,
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
