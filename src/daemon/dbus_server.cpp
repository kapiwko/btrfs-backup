// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus_server.hpp>
#include <daemon/manager_audit_log.hpp>
#include <daemon/manager_change_monitor.hpp>
#include <daemon/manager_error_mapper.hpp>
#include <daemon/manager_json_codec.hpp>
#include <daemon/polkit_authorizer.hpp>

#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <sys/epoll.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

struct ManagerRequestContext {
    btrfsbackup::daemon::ManagerService& service;
    btrfsbackup::daemon::OperationalControlService& operational;
    btrfsbackup::daemon::IManagerAuditLog& audit_log;
    btrfsbackup::daemon::ManagerJsonCodec codec;
    btrfsbackup::daemon::ManagerErrorMapper error_mapper;
};

void request_stop(int) {
    stop_requested = 1;
}

int reply_json(
    sd_bus_message* message,
    sd_bus_error* error,
    ManagerRequestContext& context,
    const std::function<std::string()>& operation
) {
    try {
        const std::string payload = operation();
        return sd_bus_reply_method_return(message, "s", payload.c_str());
    } catch (const std::exception& exception) {
        std::cerr << "btrfs-backupd: request failed: " << exception.what() << '\n';
        const auto mapped = context.error_mapper.map(exception);
        return sd_bus_error_set_const(error, mapped.dbus_name, mapped.public_message);
    }
}

std::uint32_t caller_uid(sd_bus_message* message) {
    sd_bus_creds* raw_credentials = nullptr;
    const int query_result = sd_bus_query_sender_creds(
        message,
        SD_BUS_CREDS_UID | SD_BUS_CREDS_EUID | SD_BUS_CREDS_PID | SD_BUS_CREDS_AUGMENT,
        &raw_credentials
    );
    if (query_result < 0)
        throw std::runtime_error("cannot resolve D-Bus caller credentials: " + std::string(std::strerror(-query_result)));
    std::unique_ptr<sd_bus_creds, decltype(&sd_bus_creds_unref)> credentials(raw_credentials, sd_bus_creds_unref);
    uid_t uid = 0;
    int uid_result = sd_bus_creds_get_uid(credentials.get(), &uid);
    if (uid_result == -ENODATA)
        uid_result = sd_bus_creds_get_euid(credentials.get(), &uid);
    if (uid_result < 0)
        throw std::runtime_error("cannot resolve D-Bus caller UID: " + std::string(std::strerror(-uid_result)));
    if (uid > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("D-Bus caller UID is outside the supported range");
    return static_cast<std::uint32_t>(uid);
}

std::string audit_profile_id(const std::string& profile_id) {
    try {
        return std::string(btrfsbackup::ProfileId{profile_id}.value());
    } catch (const std::exception&) {
        return "<invalid>";
    }
}

void write_audit_record(
    ManagerRequestContext& context,
    std::uint32_t uid,
    const std::string& action,
    const std::string& profile_id,
    const std::string& result,
    const std::string& error_code
) {
    const std::optional<std::string> diagnostic = context.audit_log.write({
        .caller_uid = uid,
        .action = action,
        .profile_id = profile_id,
        .result = result,
        .error_code = error_code,
    });
    if (diagnostic.has_value())
        std::cerr << "btrfs-backupd: audit write failed: " << *diagnostic << '\n';
}

int reply_operational_json(
    sd_bus_message* message,
    sd_bus_error* error,
    ManagerRequestContext& context,
    const std::string& action,
    const std::string& profile_id,
    const std::function<std::string()>& operation
) {
    const std::string audited_profile_id = audit_profile_id(profile_id);
    try {
        const std::uint32_t uid = caller_uid(message);
        try {
            const std::string payload = operation();
            write_audit_record(context, uid, action, audited_profile_id, "accepted", "none");
            return sd_bus_reply_method_return(message, "s", payload.c_str());
        } catch (const std::exception& exception) {
            const auto mapped = context.error_mapper.map(exception);
            write_audit_record(
                context,
                uid,
                action,
                audited_profile_id,
                mapped.code == btrfsbackup::daemon::ManagerErrorCode::NotAuthorized ? "denied" : "failed",
                mapped.dbus_name
            );
            std::cerr << "btrfs-backupd: request failed: " << exception.what() << '\n';
            return sd_bus_error_set_const(error, mapped.dbus_name, mapped.public_message);
        }
    } catch (const std::exception& exception) {
        std::cerr << "btrfs-backupd: request failed: " << exception.what() << '\n';
        const auto mapped = context.error_mapper.map(exception);
        return sd_bus_error_set_const(error, mapped.dbus_name, mapped.public_message);
    }
}

int get_capabilities(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    auto& context = *static_cast<ManagerRequestContext*>(userdata);
    return reply_json(message, error, context, [&] { return context.codec.encode(context.service.get_capabilities()); });
}

int list_profiles(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    auto& context = *static_cast<ManagerRequestContext*>(userdata);
    return reply_json(message, error, context, [&] { return context.codec.encode(context.service.list_profiles()); });
}

int get_status(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    const char* profile_id = nullptr;
    const int read_result = sd_bus_message_read(message, "s", &profile_id);
    if (read_result < 0)
        return read_result;
    auto& context = *static_cast<ManagerRequestContext*>(userdata);
    return reply_json(message, error, context, [&] {
        return context.codec.encode(context.service.get_status(profile_id == nullptr ? "" : profile_id));
    });
}

int get_history_sanitized(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    const char* profile_id = nullptr;
    std::uint32_t offset = 0;
    std::uint32_t limit = 0;
    const int read_result = sd_bus_message_read(message, "suu", &profile_id, &offset, &limit);
    if (read_result < 0)
        return read_result;
    auto& context = *static_cast<ManagerRequestContext*>(userdata);
    return reply_json(message, error, context, [&] {
        return context.codec.encode(
            context.service.get_history_sanitized(profile_id == nullptr ? "" : profile_id, offset, limit)
        );
    });
}

int get_device_state(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    const char* profile_id = nullptr;
    const int read_result = sd_bus_message_read(message, "s", &profile_id);
    if (read_result < 0)
        return read_result;
    auto& context = *static_cast<ManagerRequestContext*>(userdata);
    return reply_json(message, error, context, [&] {
        return context.codec.encode(context.service.get_device_state(profile_id == nullptr ? "" : profile_id));
    });
}

std::string caller_bus_name(sd_bus_message* message) {
    const char* sender = sd_bus_message_get_sender(message);
    return sender == nullptr ? std::string{} : std::string(sender);
}

int start_backup(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    const char* profile_id = nullptr;
    const int read_result = sd_bus_message_read(message, "s", &profile_id);
    if (read_result < 0)
        return read_result;
    auto& context = *static_cast<ManagerRequestContext*>(userdata);
    const std::string profile = profile_id == nullptr ? "" : profile_id;
    return reply_operational_json(message, error, context, "start-backup", profile, [&] {
        return context.codec.encode(
            context.operational.start_backup(caller_bus_name(message), profile)
        );
    });
}

int cancel_backup(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    const char* profile_id = nullptr;
    const char* run_id = nullptr;
    const int read_result = sd_bus_message_read(message, "ss", &profile_id, &run_id);
    if (read_result < 0)
        return read_result;
    auto& context = *static_cast<ManagerRequestContext*>(userdata);
    const std::string profile = profile_id == nullptr ? "" : profile_id;
    return reply_operational_json(message, error, context, "cancel-backup", profile, [&] {
        return context.codec.encode(context.operational.cancel_backup(
            caller_bus_name(message),
            profile,
            run_id == nullptr ? "" : run_id
        ));
    });
}

int validate_target(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    const char* profile_id = nullptr;
    const int read_result = sd_bus_message_read(message, "s", &profile_id);
    if (read_result < 0)
        return read_result;
    auto& context = *static_cast<ManagerRequestContext*>(userdata);
    const std::string profile = profile_id == nullptr ? "" : profile_id;
    return reply_operational_json(message, error, context, "validate-target", profile, [&] {
        return context.codec.encode(
            context.operational.validate_target(caller_bus_name(message), profile)
        );
    });
}

int eject_target(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    const char* profile_id = nullptr;
    const int read_result = sd_bus_message_read(message, "s", &profile_id);
    if (read_result < 0)
        return read_result;
    auto& context = *static_cast<ManagerRequestContext*>(userdata);
    const std::string profile = profile_id == nullptr ? "" : profile_id;
    return reply_operational_json(message, error, context, "eject-target", profile, [&] {
        return context.codec.encode(
            context.operational.eject_target(caller_bus_name(message), profile)
        );
    });
}

const sd_bus_vtable manager_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetCapabilities", "", "s", get_capabilities, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ListProfiles", "", "s", list_profiles, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetStatus", "s", "s", get_status, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetHistorySanitized", "suu", "s", get_history_sanitized, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetDeviceState", "s", "s", get_device_state, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("StartBackup", "s", "s", start_backup, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("CancelBackup", "ss", "s", cancel_backup, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ValidateTarget", "s", "s", validate_target, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("EjectTarget", "s", "s", eject_target, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("ProfilesChanged", "", 0),
    SD_BUS_SIGNAL("StatusChanged", "s", 0),
    SD_BUS_SIGNAL("HistoryChanged", "s", 0),
    SD_BUS_SIGNAL("DeviceStateChanged", "s", 0),
    SD_BUS_VTABLE_END,
};

void require_success(int result, const char* operation) {
    if (result < 0)
        throw std::runtime_error(std::string(operation) + ": " + std::strerror(-result));
}

void emit_profile_signal(sd_bus* bus, const char* signal, const std::string& profile_id) {
    const int result = sd_bus_emit_signal(
        bus,
        btrfsbackup::daemon::manager_object_path,
        btrfsbackup::daemon::manager_interface,
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
    const btrfsbackup::daemon::ManagerChange& change
) {
    if (change.kind == btrfsbackup::daemon::ManagerChangeKind::Profiles) {
        const int result = sd_bus_emit_signal(
            bus,
            btrfsbackup::daemon::manager_object_path,
            btrfsbackup::daemon::manager_interface,
            "ProfilesChanged",
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
    case btrfsbackup::daemon::ManagerChangeKind::Status:
        signal = "StatusChanged";
        break;
    case btrfsbackup::daemon::ManagerChangeKind::History:
        signal = "HistoryChanged";
        break;
    case btrfsbackup::daemon::ManagerChangeKind::Device:
        signal = "DeviceStateChanged";
        break;
    case btrfsbackup::daemon::ManagerChangeKind::Profiles:
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

int process_filesystem_changes(sd_event_source*, int, std::uint32_t, void* userdata) {
    try {
        static_cast<btrfsbackup::daemon::ManagerChangeMonitor*>(userdata)
            ->process_filesystem_events();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "btrfs-backupd: filesystem notification failed: " << exception.what()
                  << '\n';
        return -EIO;
    }
}

int process_device_changes(sd_event_source*, int, std::uint32_t, void* userdata) {
    static_cast<btrfsbackup::daemon::ManagerChangeMonitor*>(userdata)->process_device_events();
    return 0;
}

int process_mount_changes(sd_event_source*, int, std::uint32_t, void* userdata) {
    try {
        static_cast<btrfsbackup::daemon::ManagerChangeMonitor*>(userdata)->process_mount_events();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "btrfs-backupd: mount notification failed: " << exception.what() << '\n';
        return -EIO;
    }
}

} // namespace

namespace btrfsbackup::daemon {

int run_dbus_server(
    ManagerService& service,
    IOperationalControlBackend& operational_backend,
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
    OperationalControlService operational(authorizer, operational_backend);
    ManagerRequestContext context{service, operational, audit_log, {}, {}};

    std::unique_ptr<sd_bus_slot, decltype(&sd_bus_slot_unref)> slot(nullptr, sd_bus_slot_unref);
    sd_bus_slot* raw_slot = nullptr;
    require_success(
        sd_bus_add_object_vtable(
            bus.get(),
            &raw_slot,
            manager_object_path,
            manager_interface,
            manager_vtable,
            &context
        ),
        "cannot export the manager object"
    );
    slot.reset(raw_slot);
    require_success(sd_bus_request_name(bus.get(), manager_bus_name, 0), "cannot acquire the manager bus name");

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
    sd_event_source* raw_source = nullptr;
    require_success(
        sd_event_add_io(
            event.get(),
            &raw_source,
            changes.filesystem_fd(),
            EPOLLIN,
            process_filesystem_changes,
            &changes
        ),
        "cannot watch manager filesystem changes"
    );
    filesystem_source.reset(raw_source);
    raw_source = nullptr;
    require_success(
        sd_event_add_io(
            event.get(),
            &raw_source,
            changes.device_fd(),
            EPOLLIN,
            process_device_changes,
            &changes
        ),
        "cannot watch manager device changes"
    );
    device_source.reset(raw_source);
    raw_source = nullptr;
    require_success(
        sd_event_add_io(
            event.get(),
            &raw_source,
            changes.mount_fd(),
            EPOLLPRI | EPOLLERR,
            process_mount_changes,
            &changes
        ),
        "cannot watch manager mount changes"
    );
    mount_source.reset(raw_source);

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

} // namespace btrfsbackup::daemon
