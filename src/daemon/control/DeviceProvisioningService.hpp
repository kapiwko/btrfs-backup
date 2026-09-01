// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <daemon/control/OperationalControlService.hpp>

namespace btrfsbackup::daemon::control {

struct ProvisioningDevice {
    std::string path;
    std::string model;
    std::string serial;
    std::string transport;
    std::uint64_t size_bytes = 0;
    bool removable = false;
    bool mounted = false;
    bool contains_data = false;
};

struct DevicePreparationRequest {
    std::string profile_id;
    std::string profile_name;
    std::string device_path;
    std::string expected_serial;
    std::uint64_t expected_size_bytes = 0;
    std::string source_subvolume;
    std::string passphrase_label;
    bool create_automatic_key = true;
};

struct DevicePreparationStatus {
    std::string operation_id;
    std::string profile_id;
    std::string state;
    std::string phase;
    std::string error_code;
    bool can_cancel = false;
};

class IDeviceProvisioningBackend {
  public:
    virtual ~IDeviceProvisioningBackend() = default;
    [[nodiscard]] virtual std::vector<ProvisioningDevice> list_devices() = 0;
    [[nodiscard]] virtual std::vector<std::string> list_source_candidates() = 0;
    [[nodiscard]] virtual DevicePreparationStatus start(
        const DevicePreparationRequest& request,
        int passphrase_fd
    ) = 0;
    [[nodiscard]] virtual DevicePreparationStatus status(const std::string& operation_id) const = 0;
    virtual void cancel(const std::string& operation_id) = 0;
};

class DeviceProvisioningService final {
  public:
    DeviceProvisioningService(IManagerAuthorizer& authorizer, IDeviceProvisioningBackend& backend);
    [[nodiscard]] std::vector<ProvisioningDevice> list_devices(const std::string& caller);
    [[nodiscard]] std::vector<std::string> list_source_candidates(const std::string& caller);
    [[nodiscard]] DevicePreparationStatus start(
        const std::string& caller,
        const DevicePreparationRequest& request,
        int passphrase_fd
    );
    [[nodiscard]] DevicePreparationStatus status(
        const std::string& caller,
        const std::string& operation_id
    ) const;
    void cancel(const std::string& caller, const std::string& operation_id);

  private:
    void authorize(const std::string& caller, std::string_view method) const;
    void authorize_owner_or_admin(
        const std::string& caller,
        const std::string& operation_id,
        std::string_view method
    ) const;
    IManagerAuthorizer& authorizer_;
    IDeviceProvisioningBackend& backend_;
    mutable std::mutex owners_mutex_;
    std::map<std::string, std::string> operation_owners_;
};

} // namespace btrfsbackup::daemon::control
