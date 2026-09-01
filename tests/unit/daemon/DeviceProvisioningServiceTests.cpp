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
    std::vector<std::string>* events = nullptr;
    bool authorize(const std::string&, ManagerAuthorizationAction action) override {
        if (events != nullptr)
            events->push_back("authorize");
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
    int starts = 0;
    bool cancelled = false;
    bool weak_identity = false;
    std::vector<std::string> safety_reasons;
    std::vector<std::string>* events = nullptr;
    std::vector<ProvisioningDevice> list_devices() override {
        ProvisioningDevice device{
            .path = "/dev/test",
            .model = "Test disk",
            .serial = "serial",
            .transport = "usb",
            .size_bytes = 1024,
            .removable = true,
            .mounted = false,
            .contains_data = true,
            .major_minor = "8:16",
            .sysfs_devpath = "/devices/test/block/test",
            .wwn = "wwn-test",
            .serial_id = "vendor_serial",
            .serial_short = "serial",
            .device_graph = R"({"path":"/dev/test"})",
        };
        if (weak_identity) {
            device.wwn.clear();
            device.serial_id.clear();
            device.serial_short.clear();
        }
        return {device};
    }
    std::vector<std::string> list_source_candidates() override {
        return {"/home"};
    }
    std::vector<std::string> inspect_safety(const ProvisioningDevice&) const override {
        if (events != nullptr)
            events->push_back("inspect");
        return safety_reasons;
    }
    DevicePreparationStatus start(
        const DevicePreparationRequest& request,
        const ProvisioningDevice& expected_device,
        int passphrase_fd
    ) override {
        received_fd = passphrase_fd;
        ++starts;
        test_helpers::expect_eq("resolved candidate path", expected_device.path, "/dev/test");
        return {"prepare-1", request.profile_id, "queued", "inspect", {}, true};
    }
    DevicePreparationStatus status(const std::string&) const override {
        return {"prepare-1", "test", "running", "partition", {}, false};
    }
    void cancel(const std::string&) override {
        cancelled = true;
    }
};

DevicePreparationRequest request(std::string candidate_id = "candidate-1") {
    return {
        .profile_id = "test",
        .profile_name = "Test",
        .candidate_id = std::move(candidate_id),
        .source_subvolume = "/home",
        .passphrase_label = "Recovery",
        .create_automatic_key = true,
    };
}

void test_authorized_start_and_status() {
    Authorizer authorizer;
    Backend backend;
    std::vector<std::string> events;
    authorizer.events = &events;
    backend.events = &events;
    DeviceProvisioningService service(authorizer, backend, std::chrono::minutes(5), [] { return "candidate-1"; });
    const auto devices = service.list_devices(":1.5");
    test_helpers::expect_true("device list", devices.size() == 1, "candidate missing");
    test_helpers::expect_eq("candidate id", devices.front().candidate_id, "candidate-1");
    events.clear();
    const auto started = service.start(":1.5", request(devices.front().candidate_id), 17);
    test_helpers::expect_true(
        "safety before authorization",
        events == std::vector<std::string>{"inspect", "authorize"},
        "device safety was not inspected before polkit"
    );
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
    DeviceProvisioningService service(authorizer, backend, std::chrono::minutes(5), [] { return "candidate-1"; });
    static_cast<void>(service.list_devices(":1.7"));
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

void test_candidate_is_caller_bound_single_use_and_expires() {
    Authorizer authorizer;
    Backend backend;
    auto now = std::chrono::steady_clock::time_point{};
    int sequence = 0;
    DeviceProvisioningService service(
        authorizer,
        backend,
        std::chrono::seconds(30),
        [&] { return "candidate-" + std::to_string(++sequence); },
        [&] { return now; }
    );

    const auto first = service.list_devices(":1.9").front().candidate_id;
    try {
        static_cast<void>(service.start(":1.10", request(first), 17));
        test_helpers::fail("foreign candidate", "candidate was usable by another caller");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
    static_cast<void>(service.start(":1.9", request(first), 17));
    try {
        static_cast<void>(service.start(":1.9", request(first), 17));
        test_helpers::fail("reused candidate", "candidate was reusable");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }

    const auto expiring = service.list_devices(":1.9").front().candidate_id;
    now += std::chrono::seconds(31);
    try {
        static_cast<void>(service.start(":1.9", request(expiring), 17));
        test_helpers::fail("expired candidate", "expired candidate was accepted");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
    test_helpers::expect_true("candidate starts", backend.starts == 1, "invalid candidate reached backend");
}

void test_device_without_persistent_identity_is_not_a_candidate() {
    Authorizer authorizer;
    Backend backend;
    backend.weak_identity = true;
    DeviceProvisioningService service(authorizer, backend);
    test_helpers::expect_true(
        "weak device identity",
        service.list_devices(":1.11").empty(),
        "device without WWN or udev serial was offered for destructive preparation"
    );
}

void test_denied_start() {
    Authorizer authorizer;
    Backend backend;
    DeviceProvisioningService service(authorizer, backend, std::chrono::minutes(5), [] { return "candidate-denied"; });
    const std::string candidate = service.list_devices(":1.5").front().candidate_id;
    authorizer.allowed = false;
    try {
        static_cast<void>(service.start(":1.5", request(candidate), 17));
        test_helpers::fail("denied preparation", "request was accepted");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
}

void test_unsafe_device_is_rejected_before_authorization() {
    Authorizer authorizer;
    Backend backend;
    DeviceProvisioningService service(authorizer, backend, std::chrono::minutes(5), [] { return "candidate-unsafe"; });
    const std::string candidate = service.list_devices(":1.12").front().candidate_id;
    authorizer.actions.clear();
    backend.safety_reasons = {"active-swap:/dev/test"};
    try {
        static_cast<void>(service.start(":1.12", request(candidate), 17));
        test_helpers::fail("unsafe candidate", "unsafe device reached authorization");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
    test_helpers::expect_true(
        "unsafe preauthorization",
        authorizer.actions.empty() && backend.starts == 0,
        "unsafe device reached polkit or backend start"
    );
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
    test_candidate_is_caller_bound_single_use_and_expires();
    test_device_without_persistent_identity_is_not_a_candidate();
    test_denied_start();
    test_unsafe_device_is_rejected_before_authorization();
    test_invalid_request_is_rejected_before_backend();
    return test_helpers::finish("device provisioning service tests");
}
