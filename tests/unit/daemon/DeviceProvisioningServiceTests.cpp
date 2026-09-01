// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DeviceProvisioningService.hpp>

#include <core/Errors.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

#include "support/TestHelpers.hpp"

namespace {
using namespace btrfsbackup::daemon::control;

class Authorizer final : public IManagerAuthorizer {
  public:
    bool allowed = true;
    bool authorize(const std::string&, ManagerAuthorizationAction action) override {
        return allowed && action == ManagerAuthorizationAction::PrepareBackupDevice;
    }
    bool caller_is_active(const std::string&) override {
        return true;
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
    const auto devices = service.list_devices();
    test_helpers::expect_true("device list", devices.size() == 1, "candidate missing");
    const auto started = service.start(":1.5", request(), 17);
    test_helpers::expect_eq("operation id", started.operation_id, "prepare-1");
    test_helpers::expect_true("secret descriptor", backend.received_fd == 17, "descriptor not forwarded");
    test_helpers::expect_eq("operation phase", service.status("prepare-1").phase, "partition");
    service.cancel(":1.5", "prepare-1");
    test_helpers::expect_true("cancel", backend.cancelled, "cancel not forwarded");
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
    test_denied_start();
    test_invalid_request_is_rejected_before_backend();
    return test_helpers::finish("device provisioning service tests");
}
