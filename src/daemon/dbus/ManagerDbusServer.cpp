// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerDbusServer.hpp>

#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <sys/epoll.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

#include <daemon/dbus/ManagerChangeMonitor.hpp>
#include <daemon/dbus/DbusCallbackBoundary.hpp>
#include <daemon/dbus/ManagerDbusObject.hpp>
#include <daemon/dbus/PolkitAuthorizer.hpp>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void request_stop(int) {
    stop_requested = 1;
}

void require_success(int result, const char* operation) {
    if (result < 0)
        throw std::runtime_error(std::string(operation) + ": " + std::strerror(-result));
}

void emit_profile_signal(sd_bus* bus, const char* signal, const std::string& profile_id) {
    const int result = sd_bus_emit_signal(
        bus,
        btrfsbackup::manager_protocol::object_path,
        btrfsbackup::manager_protocol::interface_name,
        signal,
        "s",
        profile_id.c_str()
    );
    if (result < 0) {
        std::cerr << "btrfs-backupd: cannot emit " << signal << ": "
                  << std::strerror(-result) << '\n';
    }
}

void emit_change(
    sd_bus* bus,
    btrfsbackup::daemon::ManagerService& service,
    const btrfsbackup::daemon::dbus::ManagerChange& change
) {
    if (change.kind == btrfsbackup::daemon::dbus::ManagerChangeKind::Profiles) {
        const int result = sd_bus_emit_signal(
            bus,
            btrfsbackup::manager_protocol::object_path,
            btrfsbackup::manager_protocol::interface_name,
            btrfsbackup::manager_protocol::signal::profiles_changed,
            ""
        );
        if (result < 0) {
            std::cerr << "btrfs-backupd: cannot emit ProfilesChanged: "
                      << std::strerror(-result) << '\n';
        }
        return;
    }

    const char* signal = nullptr;
    switch (change.kind) {
    case btrfsbackup::daemon::dbus::ManagerChangeKind::Status:
        signal = btrfsbackup::manager_protocol::signal::status_changed;
        break;
    case btrfsbackup::daemon::dbus::ManagerChangeKind::History:
        signal = btrfsbackup::manager_protocol::signal::history_changed;
        break;
    case btrfsbackup::daemon::dbus::ManagerChangeKind::Device:
        signal = btrfsbackup::manager_protocol::signal::device_state_changed;
        break;
    case btrfsbackup::daemon::dbus::ManagerChangeKind::Profiles:
        return;
    }

    if (!change.profile_id.empty()) {
        emit_profile_signal(bus, signal, change.profile_id);
        return;
    }
    try {
        for (const auto& profile : service.list_profiles())
            emit_profile_signal(bus, signal, profile.profile_id);
    } catch (const std::exception& exception) {
        std::cerr << "btrfs-backupd: cannot enumerate changed profiles: "
                  << exception.what() << '\n';
    }
}

template <typename Callback>
int event_callback(const char* description, Callback&& callback) noexcept {
    return btrfsbackup::daemon::dbus::invoke_dbus_callback(
        std::forward<Callback>(callback),
        [description](const std::exception* exception) {
            std::cerr << "btrfs-backupd: " << description << " failed";
            if (exception != nullptr)
                std::cerr << ": " << exception->what();
            std::cerr << '\n';
            return -EIO;
        }
    );
}

int process_filesystem_changes(sd_event_source*, int, std::uint32_t, void* userdata) noexcept {
    return event_callback("filesystem notification", [&] {
        static_cast<btrfsbackup::daemon::dbus::ManagerChangeMonitor*>(userdata)->process_filesystem_events();
        return 0;
    });
}

int process_device_changes(sd_event_source*, int, std::uint32_t, void* userdata) noexcept {
    return event_callback("device notification", [&] {
        static_cast<btrfsbackup::daemon::dbus::ManagerChangeMonitor*>(userdata)->process_device_events();
        return 0;
    });
}

int process_mount_changes(sd_event_source*, int, std::uint32_t, void* userdata) noexcept {
    return event_callback("mount notification", [&] {
        static_cast<btrfsbackup::daemon::dbus::ManagerChangeMonitor*>(userdata)->process_mount_events();
        return 0;
    });
}

int browse_session_expiration(sd_event_source* source, std::uint64_t now, void* userdata) noexcept {
    static_cast<btrfsbackup::daemon::control::BrowseSessionService*>(userdata)->expire();
    (void)sd_event_source_set_time(source, now + 30ULL * 1000ULL * 1000ULL);
    return sd_event_source_set_enabled(source, SD_EVENT_ONESHOT);
}

int caller_owner_changed(sd_bus_message* message, void* userdata, sd_bus_error*) noexcept {
    const char* name = nullptr;
    const char* old_owner = nullptr;
    const char* new_owner = nullptr;
    if (sd_bus_message_read(message, "sss", &name, &old_owner, &new_owner) < 0)
        return 0;
    if (name != nullptr && name[0] == ':' && old_owner != nullptr && old_owner[0] != '\0' &&
        (new_owner == nullptr || new_owner[0] == '\0'))
        static_cast<btrfsbackup::daemon::control::BrowseSessionService*>(userdata)->close_for_caller(name);
    return 0;
}

} // namespace

namespace btrfsbackup::daemon::dbus {

int run_dbus_server(
    ManagerService& service,
    control::IOperationalControlBackend& operational_backend,
    control::IProfileAdministrationBackend& profile_administration_backend,
    control::ICredentialAdministrationBackend& credential_administration_backend,
    control::IDeviceProvisioningBackend& device_provisioning_backend,
    provisioning::StorageTopologyReader& storage_topology,
    control::IBrowseSessionBackend& browse_session_backend,
    IManagerAuditLog& audit_log,
    const ManagerPaths& paths,
    const std::string& bus_address
) {
    std::unique_ptr<sd_bus, decltype(&sd_bus_unref)> bus(nullptr, sd_bus_unref);
    sd_bus* raw_bus = nullptr;
    if (bus_address.empty()) {
        require_success(sd_bus_default_system(&raw_bus), "cannot connect to the system bus");
    } else {
        require_success(sd_bus_new(&raw_bus), "cannot allocate a D-Bus connection");
        bus.reset(raw_bus);
        require_success(sd_bus_set_bus_client(bus.get(), 1), "cannot configure the D-Bus client");
        require_success(sd_bus_set_address(bus.get(), bus_address.c_str()), "cannot set the D-Bus address");
        require_success(sd_bus_start(bus.get()), "cannot connect to the requested D-Bus address");
        raw_bus = nullptr;
    }
    if (!bus)
        bus.reset(raw_bus);

    PolkitAuthorizer authorizer(bus.get());
    control::OperationalControlService operational(authorizer, operational_backend);
    control::ProfileAdministrationService profile_administration(authorizer, profile_administration_backend);
    control::CredentialAdministrationService credential_administration(authorizer, credential_administration_backend);
    control::DeviceProvisioningService device_provisioning(
        authorizer,
        device_provisioning_backend,
        std::chrono::minutes(5),
        {},
        {},
        &storage_topology
    );
    control::BrowseSessionService browse_sessions(
        authorizer,
        browse_session_backend,
        std::chrono::minutes(15),
        {},
        {},
        {},
        [&](const control::BrowseSessionEvent& event) {
            const char* reason = "closed";
            switch (event.reason) {
            case control::BrowseSessionCloseReason::Requested:
                reason = "closed";
                break;
            case control::BrowseSessionCloseReason::CallerDisconnected:
                reason = "caller-disconnected";
                break;
            case control::BrowseSessionCloseReason::Expired:
                reason = "expired";
                break;
            case control::BrowseSessionCloseReason::Shutdown:
                reason = "shutdown";
                break;
            }
            (void)audit_log.write({event.caller_uid, "close-browse-session", event.profile_id, event.succeeded ? reason : "cleanup-failed", event.succeeded ? "none" : "cleanup-failed"});
        }
    );
    ManagerDbusObject object(
        service,
        operational,
        browse_sessions,
        profile_administration,
        credential_administration,
        device_provisioning,
        audit_log
    );

    std::unique_ptr<sd_bus_slot, decltype(&sd_bus_slot_unref)> owner_slot(nullptr, sd_bus_slot_unref);
    sd_bus_slot* raw_owner_slot = nullptr;
    require_success(sd_bus_match_signal(bus.get(), &raw_owner_slot, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged", caller_owner_changed, &browse_sessions), "cannot monitor D-Bus callers");
    owner_slot.reset(raw_owner_slot);

    std::unique_ptr<sd_bus_slot, decltype(&sd_bus_slot_unref)> slot(nullptr, sd_bus_slot_unref);
    sd_bus_slot* raw_slot = nullptr;
    require_success(
        sd_bus_add_object_vtable(
            bus.get(),
            &raw_slot,
            manager_protocol::object_path,
            manager_protocol::interface_name,
            ManagerDbusObject::vtable(),
            &object
        ),
        "cannot export the manager object"
    );
    slot.reset(raw_slot);
    require_success(sd_bus_request_name(bus.get(), manager_protocol::service_name, 0), "cannot acquire the manager bus name");

    std::unique_ptr<sd_event, decltype(&sd_event_unref)> event(nullptr, sd_event_unref);
    sd_event* raw_event = nullptr;
    require_success(sd_event_new(&raw_event), "cannot create the manager event loop");
    event.reset(raw_event);
    require_success(sd_bus_attach_event(bus.get(), event.get(), 0), "cannot attach D-Bus to the manager event loop");

    ManagerChangeMonitor changes(paths, [&](const ManagerChange& change) {
        emit_change(bus.get(), service, change);
    });
    using EventSource = std::unique_ptr<sd_event_source, decltype(&sd_event_source_unref)>;
    EventSource filesystem_source(nullptr, sd_event_source_unref);
    EventSource device_source(nullptr, sd_event_source_unref);
    EventSource mount_source(nullptr, sd_event_source_unref);
    EventSource expiration_source(nullptr, sd_event_source_unref);
    sd_event_source* raw_source = nullptr;
    require_success(
        sd_event_add_io(event.get(), &raw_source, changes.filesystem_fd(), EPOLLIN, process_filesystem_changes, &changes),
        "cannot watch manager filesystem changes"
    );
    filesystem_source.reset(raw_source);
    raw_source = nullptr;
    require_success(
        sd_event_add_io(event.get(), &raw_source, changes.device_fd(), EPOLLIN, process_device_changes, &changes),
        "cannot watch manager device changes"
    );
    device_source.reset(raw_source);
    raw_source = nullptr;
    require_success(
        sd_event_add_io(event.get(), &raw_source, changes.mount_fd(), EPOLLPRI | EPOLLERR, process_mount_changes, &changes),
        "cannot watch manager mount changes"
    );
    mount_source.reset(raw_source);
    raw_source = nullptr;
    std::uint64_t now = 0;
    require_success(sd_event_now(event.get(), CLOCK_MONOTONIC, &now), "cannot read manager event time");
    require_success(sd_event_add_time(event.get(), &raw_source, CLOCK_MONOTONIC, now + 30ULL * 1000ULL * 1000ULL, 1000ULL * 1000ULL, browse_session_expiration, &browse_sessions), "cannot schedule browse session expiration");
    expiration_source.reset(raw_source);

    stop_requested = 0;
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
    while (!stop_requested) {
        const int processed = sd_event_run(event.get(), UINT64_MAX);
        if (processed < 0 && processed != -EINTR)
            require_success(processed, "manager event processing failed");
    }
    sd_bus_detach_event(bus.get());
    return 0;
}

} // namespace btrfsbackup::daemon::dbus
