// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DeviceProvisioningService.hpp>

#include <filesystem>

#include <core/Errors.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

namespace btrfsbackup::daemon::control {

DeviceProvisioningService::DeviceProvisioningService(
    IManagerAuthorizer& authorizer,
    IDeviceProvisioningBackend& backend
) : authorizer_(authorizer), backend_(backend) {
}

std::vector<ProvisioningDevice> DeviceProvisioningService::list_devices() {
    return backend_.list_devices();
}
std::vector<std::string> DeviceProvisioningService::list_source_candidates() {
    return backend_.list_source_candidates();
}

void DeviceProvisioningService::authorize(const std::string& caller) const {
    if (caller.empty() || !authorizer_.authorize(caller, ManagerAuthorizationAction::PrepareBackupDevice) ||
        !authorizer_.caller_is_active(caller))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotAuthorized, "operation is not authorized");
}

DevicePreparationStatus DeviceProvisioningService::start(
    const std::string& caller,
    const DevicePreparationRequest& request,
    int passphrase_fd
) {
    if (request.profile_id.empty() || request.profile_name.empty() || request.device_path.empty() ||
        request.expected_size_bytes == 0 || request.source_subvolume.empty() || request.passphrase_label.empty())
        throw ValidationError("device preparation request is incomplete");
    static_cast<void>(ProfileId{request.profile_id});
    const std::filesystem::path device(request.device_path);
    const std::filesystem::path source(request.source_subvolume);
    if (!device.is_absolute() || device.lexically_normal() != device ||
        !source.is_absolute() || source.lexically_normal() != source)
        throw ValidationError("device preparation paths are invalid");
    if (passphrase_fd < 0)
        throw ValidationError("device preparation passphrase descriptor is invalid");
    if (request.profile_name.size() > 120 || request.passphrase_label.size() > 80)
        throw ValidationError("device preparation text is too long");
    authorize(caller);
    return backend_.start(request, passphrase_fd);
}

DevicePreparationStatus DeviceProvisioningService::status(const std::string& operation_id) const {
    if (operation_id.empty())
        throw ValidationError("operation identifier is empty");
    return backend_.status(operation_id);
}

void DeviceProvisioningService::cancel(const std::string& caller, const std::string& operation_id) {
    authorize(caller);
    backend_.cancel(operation_id);
}

} // namespace btrfsbackup::daemon::control
