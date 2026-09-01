// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DeviceProvisioningService.hpp>

#include <core/Errors.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

#include "support/TestHelpers.hpp"

namespace {
using btrfsbackup::daemon::control::DevicePreparationRequest;
using btrfsbackup::daemon::control::DevicePreparationStatus;
using btrfsbackup::daemon::control::DeviceProvisioningService;
using btrfsbackup::daemon::control::IDeviceProvisioningBackend;
using btrfsbackup::daemon::control::IManagerAuthorizer;
using btrfsbackup::daemon::control::ManagerAuthorizationAction;
using btrfsbackup::daemon::control::ProvisioningDevice;

class Authorizer final : public IManagerAuthorizer {
  public:
    bool allowed = true;
    bool active = true;
    std::vector<ManagerAuthorizationAction> actions;
    bool authorize(const std::string&, ManagerAuthorizationAction action) override {
        actions.push_back(action);
        return allowed && action == ManagerAuthorizationAction::PrepareBackupDevice;
    }
    bool caller_is_active(const std::string&) override {
        return active;
    }
};

class Backend final : public IDeviceProvisioningBackend {
  public:
    int received_fd = -1;
    bool cancelled = false;
    std::vector<ProvisioningDevice> list_devices() override {
        return {{"/dev/test", "Test disk", "serial", "usb", 1024, true, false, true}};
    }
    std::vector<std::string> list_source_candidates() override {
        return {"/home"};
    }
    DevicePreparationStatus start(const DevicePreparationRequest& request, int passphrase_fd) override {
        received_fd = passphrase_fd;
        return {"prepare-1", request.profile_id, "queued", "inspect", {}, true};
    }
    DevicePreparationStatus status(const std::string&) const override {
        return {"prepare-1", "test", "running", "partition", {}, false};
    }
    void cancel(const std::string&) override {
        cancelled = true;
    }
};

DevicePreparationRequest request() {
    return {"test", "Test", "/dev/test", "serial", 1024, "/home", "Recovery", true};
}

void test_authorized_start_and_status() {
    Authorizer authorizer;
    Backend backend;
    DeviceProvisioningService service(authorizer, backend);
    const auto devices = service.list_devices(":1.5");
    test_helpers::expect_true("device list", devices.size() == 1, "candidate missing");
    const auto started = service.start(":1.5", request(), 17);
    test_helpers::expect_eq("operation id", started.operation_id, "prepare-1");
    test_helpers::expect_true("secret descriptor", backend.received_fd == 17, "descriptor not forwarded");
    test_helpers::expect_eq("operation phase", service.status(":1.5", "prepare-1").phase, "partition");
    service.cancel(":1.5", "prepare-1");
    test_helpers::expect_true("cancel", backend.cancelled, "cancel not forwarded");
}

void test_inspection_requires_device_authorization() {
    Authorizer authorizer;
    authorizer.allowed = false;
    Backend backend;
    DeviceProvisioningService service(authorizer, backend);
    try {
        static_cast<void>(service.list_devices(":1.6"));
        test_helpers::fail("denied device listing", "listing was accepted");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
    try {
        static_cast<void>(service.list_source_candidates(":1.6"));
        test_helpers::fail("denied source listing", "listing was accepted");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
}

void test_operation_access_is_limited_to_owner_or_admin() {
    Authorizer authorizer;
    Backend backend;
    DeviceProvisioningService service(authorizer, backend);
    const auto started = service.start(":1.7", request(), 17);

    authorizer.allowed = false;
    test_helpers::expect_eq(
        "owner status",
        service.status(":1.7", started.operation_id).operation_id,
        started.operation_id
    );
    try {
        static_cast<void>(service.status(":1.8", started.operation_id));
        test_helpers::fail("foreign status", "status was disclosed");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
    try {
        service.cancel(":1.8", started.operation_id);
        test_helpers::fail("foreign cancellation", "cancellation was accepted");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }

    authorizer.allowed = true;
    test_helpers::expect_eq(
        "administrator status",
        service.status(":1.8", started.operation_id).operation_id,
        started.operation_id
    );
}

void test_denied_start() {
    Authorizer authorizer;
    authorizer.allowed = false;
    Backend backend;
    DeviceProvisioningService service(authorizer, backend);
    try {
        static_cast<void>(service.start(":1.5", request(), 17));
        test_helpers::fail("denied preparation", "request was accepted");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
}

void test_invalid_request_is_rejected_before_backend() {
    Authorizer authorizer;
    Backend backend;
    DeviceProvisioningService service(authorizer, backend);
    auto invalid = request();
    invalid.source_subvolume = "relative/source";
    try {
        static_cast<void>(service.start(":1.5", invalid, 17));
        test_helpers::fail("invalid preparation", "relative source was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
    try {
        static_cast<void>(service.start(":1.5", request(), -1));
        test_helpers::fail("invalid descriptor", "invalid descriptor was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
}
} // namespace

int main() {
    test_authorized_start_and_status();
    test_inspection_requires_device_authorization();
    test_operation_access_is_limited_to_owner_or_admin();
    test_denied_start();
    test_invalid_request_is_rejected_before_backend();
    return test_helpers::finish("device provisioning service tests");
}
