// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <daemon/control/OperationalControlService.hpp>

namespace btrfsbackup::daemon::control {

struct ProvisioningDevice {
    std::string candidate_id;
    std::string path;
    std::string model;
    std::string serial;
    std::string transport;
    std::uint64_t size_bytes = 0;
    bool removable = false;
    bool mounted = false;
    bool contains_data = false;
    std::string major_minor;
    std::string sysfs_devpath;
    std::string wwn;
    std::string serial_id;
    std::string serial_short;
    std::string device_graph;
};

struct DevicePreparationRequest {
    std::string profile_id;
    std::string profile_name;
    std::string candidate_id;
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
    std::string recovery_action;
    bool can_cancel = false;
};

struct DevicePreparationOwner {
    std::string bus_name;
    std::uint32_t uid = 0;
};

class IDeviceProvisioningBackend {
  public:
    virtual ~IDeviceProvisioningBackend() = default;
    [[nodiscard]] virtual std::vector<ProvisioningDevice> list_devices() = 0;
    [[nodiscard]] virtual std::vector<std::string> list_source_candidates() = 0;
    [[nodiscard]] virtual std::vector<std::string> inspect_safety(
        const ProvisioningDevice& expected_device
    ) const = 0;
    [[nodiscard]] virtual DevicePreparationStatus start(
        const DevicePreparationRequest& request,
        const ProvisioningDevice& expected_device,
        const DevicePreparationOwner& owner,
        int passphrase_fd
    ) = 0;
    [[nodiscard]] virtual DevicePreparationStatus status(const std::string& operation_id) const = 0;
    [[nodiscard]] virtual bool owned_by(
        const std::string& operation_id,
        const DevicePreparationOwner& owner
    ) const = 0;
    virtual void cancel(const std::string& operation_id) = 0;
};

using ProvisioningCandidateClock = std::function<std::chrono::steady_clock::time_point()>;
using ProvisioningCandidateIdGenerator = std::function<std::string()>;

class DeviceProvisioningService final {
  public:
    DeviceProvisioningService(
        IManagerAuthorizer& authorizer,
        IDeviceProvisioningBackend& backend,
        std::chrono::seconds candidate_lifetime = std::chrono::minutes(5),
        ProvisioningCandidateIdGenerator candidate_ids = {},
        ProvisioningCandidateClock clock = {}
    );
    [[nodiscard]] std::vector<ProvisioningDevice> list_devices(const std::string& caller);
    [[nodiscard]] std::vector<std::string> list_source_candidates(const std::string& caller);
    [[nodiscard]] DevicePreparationStatus start(
        const std::string& caller,
        std::uint32_t caller_uid,
        const DevicePreparationRequest& request,
        int passphrase_fd
    );
    [[nodiscard]] DevicePreparationStatus status(
        const std::string& caller,
        std::uint32_t caller_uid,
        const std::string& operation_id
    ) const;
    void cancel(const std::string& caller, std::uint32_t caller_uid, const std::string& operation_id);

  private:
    struct Candidate {
        ProvisioningDevice device;
        std::string caller;
        std::chrono::steady_clock::time_point expires_at;
    };
    [[nodiscard]] ProvisioningDevice take_candidate(
        const std::string& caller,
        const std::string& candidate_id
    );
    [[nodiscard]] ProvisioningDevice find_candidate(
        const std::string& caller,
        const std::string& candidate_id
    );
    void expire_candidates(std::chrono::steady_clock::time_point now);
    void authorize(const std::string& caller, std::string_view method) const;
    void authorize_owner_or_admin(
        const std::string& caller,
        std::uint32_t caller_uid,
        const std::string& operation_id,
        std::string_view method
    ) const;
    IManagerAuthorizer& authorizer_;
    IDeviceProvisioningBackend& backend_;
    std::chrono::seconds candidate_lifetime_;
    ProvisioningCandidateIdGenerator candidate_ids_;
    ProvisioningCandidateClock clock_;
    std::mutex candidates_mutex_;
    std::map<std::string, Candidate> candidates_;
};

} // namespace btrfsbackup::daemon::control
