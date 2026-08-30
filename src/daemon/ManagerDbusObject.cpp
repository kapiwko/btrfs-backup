// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/ManagerDbusObject.hpp>
#include <daemon/ManagerDbusServer.hpp>
#include <daemon/ManagerAuditLog.hpp>
#include <daemon/ManagerErrorMapper.hpp>
#include <daemon/ManagerJsonCodec.hpp>

#include <systemd/sd-bus.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

using ManagerRequestContext = btrfsbackup::daemon::ManagerDbusObject;

template <typename Operation>
int reply_json(
    sd_bus_message* message,
    sd_bus_error* error,
    ManagerRequestContext& context,
    Operation&& operation
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

template <typename Operation>
int reply_operational_json(
    sd_bus_message* message,
    sd_bus_error* error,
    ManagerRequestContext& context,
    const std::string& action,
    const std::string& profile_id,
    Operation&& operation
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

} // namespace

namespace btrfsbackup::daemon {

const sd_bus_vtable* ManagerDbusObject::vtable() const {
    return manager_vtable;
}

} // namespace btrfsbackup::daemon
