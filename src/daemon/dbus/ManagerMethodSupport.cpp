// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerMethodSupport.hpp>

#include <systemd/sd-bus.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>

#include <core/Identifiers.hpp>
#include <core/ManagerProtocol.hpp>

namespace btrfsbackup::daemon::dbus {

ManagerMethodSupport::ManagerMethodSupport(IManagerAuditLog& audit_log)
    : audit_log_(audit_log) {
}

ManagerJsonCodec& ManagerMethodSupport::codec() noexcept {
    return codec_;
}

int ManagerMethodSupport::set_callback_error(sd_bus_error* error, const std::exception* exception) {
    if (exception != nullptr)
        std::cerr << "btrfs-backupd: request failed: " << exception->what() << '\n';
    else
        std::cerr << "btrfs-backupd: request failed with an unknown exception\n";
    const auto mapped = exception != nullptr
        ? error_mapper_.map(*exception)
        : ManagerErrorMapper::describe(ManagerErrorCode::InternalError);
    return sd_bus_error_set_const(error, mapped.dbus_name, mapped.public_message);
}

int ManagerMethodSupport::reply_json(sd_bus_message* message, const std::string& payload) {
    return sd_bus_reply_method_return(message, "s", payload.c_str());
}

std::uint32_t ManagerMethodSupport::caller_uid(sd_bus_message* message) {
    return caller_access_identity(message).uid;
}

control::BrowseAccessIdentity ManagerMethodSupport::caller_access_identity(sd_bus_message* message) {
    sd_bus_creds* raw_credentials = nullptr;
    const int query_result = sd_bus_query_sender_creds(
        message,
        SD_BUS_CREDS_UID | SD_BUS_CREDS_EUID | SD_BUS_CREDS_GID | SD_BUS_CREDS_EGID |
            SD_BUS_CREDS_SUPPLEMENTARY_GIDS | SD_BUS_CREDS_PID | SD_BUS_CREDS_AUGMENT,
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
    control::BrowseAccessIdentity identity{.uid = static_cast<std::uint32_t>(uid), .groups = {}};
    gid_t gid = 0;
    int gid_result = sd_bus_creds_get_gid(credentials.get(), &gid);
    if (gid_result == -ENODATA)
        gid_result = sd_bus_creds_get_egid(credentials.get(), &gid);
    if (gid_result >= 0) {
        if (gid > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("D-Bus caller GID is outside the supported range");
        identity.groups.push_back(static_cast<std::uint32_t>(gid));
    } else if (gid_result != -ENODATA) {
        throw std::runtime_error("cannot resolve D-Bus caller GID: " + std::string(std::strerror(-gid_result)));
    }
    const gid_t* supplementary = nullptr;
    const int group_count = sd_bus_creds_get_supplementary_gids(credentials.get(), &supplementary);
    if (group_count < 0 && group_count != -ENODATA)
        throw std::runtime_error("cannot resolve D-Bus caller supplementary groups: " + std::string(std::strerror(-group_count)));
    for (int index = 0; index < std::max(group_count, 0); ++index) {
        if (supplementary[index] > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("D-Bus caller supplementary GID is outside the supported range");
        identity.groups.push_back(static_cast<std::uint32_t>(supplementary[index]));
    }
    std::ranges::sort(identity.groups);
    identity.groups.erase(std::ranges::unique(identity.groups).begin(), identity.groups.end());
    return identity;
}

std::string ManagerMethodSupport::caller_bus_name(sd_bus_message* message) {
    const char* sender = sd_bus_message_get_sender(message);
    return sender == nullptr ? std::string{} : std::string(sender);
}

void ManagerMethodSupport::emit_device_state_changed(
    sd_bus_message* message,
    const std::string& profile_id
) noexcept {
    sd_bus* bus = sd_bus_message_get_bus(message);
    if (bus == nullptr)
        return;
    const int result = sd_bus_emit_signal(
        bus,
        manager_protocol::object_path,
        manager_protocol::interface_name,
        manager_protocol::signal::device_state_changed,
        "s",
        profile_id.c_str()
    );
    if (result < 0)
        std::cerr << "btrfs-backupd: cannot emit DeviceStateChanged after eject: "
                  << std::strerror(-result) << '\n';
}

std::string ManagerMethodSupport::audit_profile_id(const std::string& profile_id) {
    try {
        return std::string(ProfileId{profile_id}.value());
    } catch (const std::exception&) {
        return "<invalid>";
    }
}

void ManagerMethodSupport::write_audit_record(
    std::uint32_t uid,
    const std::string& action,
    const std::string& profile_id,
    const std::string& result,
    const std::string& error_code
) {
    const std::optional<std::string> diagnostic = audit_log_.write({
        .caller_uid = uid,
        .action = action,
        .profile_id = profile_id,
        .result = result,
        .error_code = error_code,
    });
    if (diagnostic.has_value())
        std::cerr << "btrfs-backupd: audit write failed: " << *diagnostic << '\n';
}

int ManagerMethodSupport::reply_operational_json(
    sd_bus_message* message,
    sd_bus_error* error,
    const std::string& action,
    const std::string& profile_id,
    const JsonOperation& operation
) {
    const std::string audited_profile_id = audit_profile_id(profile_id);
    const std::uint32_t uid = caller_uid(message);
    try {
        const std::string payload = operation();
        write_audit_record(uid, action, audited_profile_id, "accepted", "none");
        return reply_json(message, payload);
    } catch (const std::exception& exception) {
        const auto mapped = error_mapper_.map(exception);
        std::cerr << "btrfs-backupd: " << action << " failed for profile "
                  << audited_profile_id << ": " << exception.what() << '\n';
        write_audit_record(
            uid,
            action,
            audited_profile_id,
            mapped.code == ManagerErrorCode::NotAuthorized ? "denied" : "failed",
            mapped.dbus_name
        );
        std::cerr << "btrfs-backupd: request failed: " << exception.what() << '\n';
        return sd_bus_error_set_const(error, mapped.dbus_name, mapped.public_message);
    }
}

} // namespace btrfsbackup::daemon::dbus
