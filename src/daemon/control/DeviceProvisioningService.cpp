// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DeviceProvisioningService.hpp>

#include <sys/random.h>

#include <array>
#include <cerrno>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

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
    ProvisioningCandidateClock clock
) : authorizer_(authorizer), backend_(backend), candidate_lifetime_(candidate_lifetime),
    candidate_ids_(candidate_ids ? std::move(candidate_ids) : ProvisioningCandidateIdGenerator{random_candidate_id}),
    clock_(clock ? std::move(clock) : ProvisioningCandidateClock{[] { return std::chrono::steady_clock::now(); }}) {
    if (candidate_lifetime_ <= std::chrono::seconds::zero())
        throw std::invalid_argument("provisioning candidate lifetime must be positive");
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

DevicePreparationStatus DeviceProvisioningService::start(
    const std::string& caller,
    std::uint32_t caller_uid,
    const DevicePreparationRequest& request,
    int passphrase_fd
) {
    if (request.profile_id.empty() || request.profile_name.empty() || request.candidate_id.empty() ||
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
    const ProvisioningDevice candidate = find_candidate(caller, request.candidate_id);
    const std::vector<std::string> safety_reasons = backend_.inspect_safety(candidate);
    if (!safety_reasons.empty())
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Conflict,
            "selected device is not safe for destructive preparation: " + safety_reasons.front()
        );
    authorize(caller, manager_protocol::method::start_device_preparation);
    const ProvisioningDevice expected_device = take_candidate(caller, request.candidate_id);
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
