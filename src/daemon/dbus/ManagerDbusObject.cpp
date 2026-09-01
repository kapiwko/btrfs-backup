// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerDbusObject.hpp>

#include <daemon/dbus/DbusCallbackBoundary.hpp>
#include <daemon/dbus/ManagerDbusServer.hpp>
#include <config/json/JsonIo.hpp>

#include <systemd/sd-bus.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {

namespace manager_protocol = btrfsbackup::manager_protocol;
using ManagerDbusObject = btrfsbackup::daemon::dbus::ManagerDbusObject;

int get_capabilities(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_get_capabilities(message, error);
}

int list_profiles(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_list_profiles(message, error);
}

int get_status(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_get_status(message, error);
}

int get_history_sanitized(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_get_history_sanitized(message, error);
}

int get_device_state(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_get_device_state(message, error);
}

int start_backup(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_start_backup(message, error);
}

int cancel_backup(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_cancel_backup(message, error);
}

int validate_target(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_validate_target(message, error);
}

int eject_target(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_eject_target(message, error);
}

int get_profile_details(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_get_profile_details(message, error);
}

int update_profile_settings(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_update_profile_settings(message, error);
}

int add_profile_source(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_add_profile_source(message, error);
}

int update_profile_source(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_update_profile_source(message, error);
}

int remove_profile_source(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_remove_profile_source(message, error);
}

int delete_profile(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_delete_profile(message, error);
}

int set_profile_enabled(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_set_profile_enabled(message, error);
}

int open_browse_session(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_open_browse_session(message, error);
}

int close_browse_session(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_close_browse_session(message, error);
}

int resolve_backup_coverage(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_resolve_backup_coverage(message, error);
}

int list_target_credentials(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_list_target_credentials(message, error);
}

int add_target_passphrase(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_add_target_passphrase(message, error);
}

int add_target_key(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_add_target_key(message, error);
}

int generate_target_key(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_generate_target_key(message, error);
}

int remove_target_credential(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_remove_target_credential(message, error);
}
int list_provisioning_devices(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_list_provisioning_devices(message, error);
}
int list_source_candidates(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_list_source_candidates(message, error);
}
int start_device_preparation(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_start_device_preparation(message, error);
}
int get_device_preparation(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_get_device_preparation(message, error);
}
int cancel_device_preparation(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->handle_cancel_device_preparation(message, error);
}

const sd_bus_vtable manager_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD(manager_protocol::method::get_capabilities, "", "s", get_capabilities, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::list_profiles, "", "s", list_profiles, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::get_status, "s", "s", get_status, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::get_history_sanitized, "suu", "s", get_history_sanitized, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::get_device_state, "s", "s", get_device_state, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::start_backup, "s", "s", start_backup, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::cancel_backup, "ss", "s", cancel_backup, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::validate_target, "s", "s", validate_target, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::eject_target, "s", "s", eject_target, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::get_profile_details, "s", "s", get_profile_details, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::update_profile_settings, "ssss", "s", update_profile_settings, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::add_profile_source, "ssss", "s", add_profile_source, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::update_profile_source, "sssss", "s", update_profile_source, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::remove_profile_source, "ssss", "s", remove_profile_source, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::delete_profile, "sss", "s", delete_profile, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::set_profile_enabled, "sb", "s", set_profile_enabled, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::open_browse_session, "s", "s", open_browse_session, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::close_browse_session, "s", "s", close_browse_session, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::resolve_backup_coverage, "s", "s", resolve_backup_coverage, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::list_target_credentials, "s", "s", list_target_credentials, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::add_target_passphrase, "shhs", "s", add_target_passphrase, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::add_target_key, "shhsb", "s", add_target_key, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::generate_target_key, "shsb", "s", generate_target_key, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::remove_target_credential, "ssh", "s", remove_target_credential, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::list_provisioning_devices, "", "s", list_provisioning_devices, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::list_source_candidates, "", "s", list_source_candidates, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::start_device_preparation, "sh", "s", start_device_preparation, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::get_device_preparation, "s", "s", get_device_preparation, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::cancel_device_preparation, "s", "s", cancel_device_preparation, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL(manager_protocol::signal::profiles_changed, "", 0),
    SD_BUS_SIGNAL(manager_protocol::signal::status_changed, "s", 0),
    SD_BUS_SIGNAL(manager_protocol::signal::history_changed, "s", 0),
    SD_BUS_SIGNAL(manager_protocol::signal::device_state_changed, "s", 0),
    SD_BUS_VTABLE_END,
};

} // namespace

namespace btrfsbackup::daemon::dbus {

ManagerDbusObject::ManagerDbusObject(
    ManagerService& service,
    control::OperationalControlService& operational,
    control::BrowseSessionService& browse_sessions,
    control::ProfileAdministrationService& profile_administration,
    control::CredentialAdministrationService& credential_administration,
    control::DeviceProvisioningService& device_provisioning,
    IManagerAuditLog& audit_log
)
    : service_(service), operational_(operational), browse_sessions_(browse_sessions),
      profile_administration_(profile_administration), credential_administration_(credential_administration),
      device_provisioning_(device_provisioning), audit_log_(audit_log) {
}

const sd_bus_vtable* ManagerDbusObject::vtable() noexcept {
    return manager_vtable;
}

int ManagerDbusObject::set_callback_error(sd_bus_error* error, const std::exception* exception) {
    if (exception != nullptr)
        std::cerr << "btrfs-backupd: request failed: " << exception->what() << '\n';
    else
        std::cerr << "btrfs-backupd: request failed with an unknown exception\n";
    const auto mapped = exception != nullptr
        ? error_mapper_.map(*exception)
        : ManagerErrorMapper::describe(ManagerErrorCode::InternalError);
    return sd_bus_error_set_const(error, mapped.dbus_name, mapped.public_message);
}

int ManagerDbusObject::reply_json(sd_bus_message* message, const std::string& payload) {
    return sd_bus_reply_method_return(message, "s", payload.c_str());
}

void ManagerDbusObject::emit_device_state_changed(
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

std::uint32_t ManagerDbusObject::caller_uid(sd_bus_message* message) {
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

std::string ManagerDbusObject::caller_bus_name(sd_bus_message* message) {
    const char* sender = sd_bus_message_get_sender(message);
    return sender == nullptr ? std::string{} : std::string(sender);
}

std::string ManagerDbusObject::audit_profile_id(const std::string& profile_id) {
    try {
        return std::string(ProfileId{profile_id}.value());
    } catch (const std::exception&) {
        return "<invalid>";
    }
}

void ManagerDbusObject::write_audit_record(
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

int ManagerDbusObject::reply_operational_json(
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

int ManagerDbusObject::handle_get_capabilities(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] { return reply_json(message, codec_.encode(service_.get_capabilities())); },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_list_profiles(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            auto profiles = service_.list_profiles();
            for (auto& profile : profiles) {
                const auto health = profile_administration_.configuration_health(profile.profile_id);
                profile.configuration_valid = health.valid;
                profile.configuration_error_code = health.error_code;
            }
            return reply_json(message, codec_.encode(profiles));
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_get_status(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            return reply_json(message, codec_.encode(service_.get_status(profile_id == nullptr ? "" : profile_id)));
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_get_history_sanitized(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            std::uint32_t offset = 0;
            std::uint32_t limit = 0;
            const int read_result = sd_bus_message_read(message, "suu", &profile_id, &offset, &limit);
            if (read_result < 0)
                return read_result;
            return reply_json(
                message,
                codec_.encode(service_.get_history_sanitized(profile_id == nullptr ? "" : profile_id, offset, limit))
            );
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_get_device_state(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            return reply_json(message, codec_.encode(service_.get_device_state(profile_id == nullptr ? "" : profile_id)));
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_start_backup(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, manager_protocol::feature::start_backup, profile, [&] {
                return codec_.encode(operational_.start_backup(caller_bus_name(message), profile));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_cancel_backup(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* run_id = nullptr;
            const int read_result = sd_bus_message_read(message, "ss", &profile_id, &run_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, manager_protocol::feature::cancel_backup, profile, [&] {
                return codec_.encode(operational_.cancel_backup(
                    caller_bus_name(message),
                    profile,
                    run_id == nullptr ? "" : run_id
                ));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_validate_target(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, manager_protocol::feature::validate_target, profile, [&] {
                return codec_.encode(operational_.validate_target(caller_bus_name(message), profile));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_eject_target(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, manager_protocol::feature::eject_target, profile, [&] {
                const auto result = operational_.eject_target(caller_bus_name(message), profile);
                emit_device_state_changed(message, profile);
                return codec_.encode(result);
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_get_profile_details(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_json(message, codec_.encode(profile_administration_.get_profile_details(profile)));
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_update_profile_settings(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* generation = nullptr;
            const char* fingerprint = nullptr;
            const char* request = nullptr;
            const int read_result = sd_bus_message_read(message, "ssss", &profile_id, &generation, &fingerprint, &request);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, "update-profile-settings", profile, [&] {
                return codec_.encode(profile_administration_.update_profile_settings(
                    caller_bus_name(message),
                    profile,
                    generation == nullptr ? "" : generation,
                    fingerprint == nullptr ? "" : fingerprint,
                    request == nullptr ? "" : request
                ));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_add_profile_source(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* generation = nullptr;
            const char* fingerprint = nullptr;
            const char* request = nullptr;
            const int read_result = sd_bus_message_read(message, "ssss", &profile_id, &generation, &fingerprint, &request);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, "add-profile-source", profile, [&] {
                return codec_.encode(profile_administration_.add_profile_source(
                    caller_bus_name(message),
                    profile,
                    generation == nullptr ? "" : generation,
                    fingerprint == nullptr ? "" : fingerprint,
                    request == nullptr ? "" : request
                ));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_update_profile_source(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* source_id = nullptr;
            const char* generation = nullptr;
            const char* fingerprint = nullptr;
            const char* request = nullptr;
            const int read_result = sd_bus_message_read(message, "sssss", &profile_id, &source_id, &generation, &fingerprint, &request);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, "update-profile-source", profile, [&] {
                return codec_.encode(profile_administration_.update_profile_source(
                    caller_bus_name(message),
                    profile,
                    source_id == nullptr ? "" : source_id,
                    generation == nullptr ? "" : generation,
                    fingerprint == nullptr ? "" : fingerprint,
                    request == nullptr ? "" : request
                ));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_remove_profile_source(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* source_id = nullptr;
            const char* generation = nullptr;
            const char* fingerprint = nullptr;
            const int read_result = sd_bus_message_read(message, "ssss", &profile_id, &source_id, &generation, &fingerprint);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, "remove-profile-source", profile, [&] {
                return codec_.encode(profile_administration_.remove_profile_source(
                    caller_bus_name(message),
                    profile,
                    source_id == nullptr ? "" : source_id,
                    generation == nullptr ? "" : generation,
                    fingerprint == nullptr ? "" : fingerprint
                ));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_delete_profile(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* generation = nullptr;
            const char* fingerprint = nullptr;
            const int read_result = sd_bus_message_read(message, "sss", &profile_id, &generation, &fingerprint);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, "delete-profile", profile, [&] {
                profile_administration_.delete_profile(
                    caller_bus_name(message),
                    profile,
                    generation == nullptr ? "" : generation,
                    fingerprint == nullptr ? "" : fingerprint
                );
                return config::json::dump_json({
                    {"schemaVersion", manager_protocol::operation_result_schema_version},
                    {"operation", "delete-profile"},
                    {"profileId", profile},
                    {"accepted", true},
                });
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_set_profile_enabled(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            int enabled = 0;
            const int read_result = sd_bus_message_read(message, "sb", &profile_id, &enabled);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, manager_protocol::feature::profile_activation, profile, [&] {
                profile_administration_.set_profile_enabled(caller_bus_name(message), profile, enabled != 0);
                return config::json::dump_json({
                    {"schemaVersion", manager_protocol::operation_result_schema_version},
                    {"operation", manager_protocol::feature::profile_activation},
                    {"profileId", profile},
                    {"enabled", enabled != 0},
                    {"accepted", true},
                });
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_open_browse_session(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, manager_protocol::feature::browse_backups, profile, [&] {
                return codec_.encode(browse_sessions_.open(caller_bus_name(message), caller_uid(message), profile));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_close_browse_session(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* session_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &session_id);
            if (read_result < 0)
                return read_result;
            browse_sessions_.close(caller_bus_name(message), session_id == nullptr ? "" : session_id);
            return reply_json(message, config::json::dump_json({
                                           {"schemaVersion", manager_protocol::operation_result_schema_version},
                                           {"operation", "close-browse-session"},
                                           {"accepted", true},
                                       }));
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_resolve_backup_coverage(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* local_path = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &local_path);
            if (read_result < 0)
                return read_result;
            std::vector<ProfileId> profile_ids;
            for (const auto& profile : service_.list_profiles())
                profile_ids.emplace_back(profile.profile_id);
            return reply_operational_json(message, error, "resolve-backup-coverage", "", [&] {
                return codec_.encode(browse_sessions_.resolve_coverage(
                    caller_bus_name(message),
                    local_path == nullptr ? "" : local_path,
                    profile_ids
                ));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_list_target_credentials(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, "list-target-credentials", profile, [&] {
                return codec_.encode(credential_administration_.list_credentials(
                    caller_bus_name(message),
                    profile
                ));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_add_target_passphrase(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* label = nullptr;
            int authorization_fd = -1;
            int new_secret_fd = -1;
            const int read_result = sd_bus_message_read(
                message,
                "shhs",
                &profile_id,
                &authorization_fd,
                &new_secret_fd,
                &label
            );
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, "add-target-passphrase", profile, [&] {
                return codec_.encode(credential_administration_.add_passphrase(
                    caller_bus_name(message),
                    profile,
                    authorization_fd,
                    new_secret_fd,
                    label == nullptr ? "" : label
                ));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_add_target_key(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* label = nullptr;
            int authorization_fd = -1;
            int key_fd = -1;
            int automatic = 0;
            const int read_result = sd_bus_message_read(
                message,
                "shhsb",
                &profile_id,
                &authorization_fd,
                &key_fd,
                &label,
                &automatic
            );
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, "add-target-key", profile, [&] {
                return codec_.encode(credential_administration_.add_key(
                    caller_bus_name(message),
                    profile,
                    authorization_fd,
                    key_fd,
                    label == nullptr ? "" : label,
                    automatic != 0
                ));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_generate_target_key(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* label = nullptr;
            int authorization_fd = -1;
            int automatic = 0;
            const int read_result = sd_bus_message_read(
                message,
                "shsb",
                &profile_id,
                &authorization_fd,
                &label,
                &automatic
            );
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, "generate-target-key", profile, [&] {
                return codec_.encode(credential_administration_.generate_key(
                    caller_bus_name(message),
                    profile,
                    authorization_fd,
                    label == nullptr ? "" : label,
                    automatic != 0
                ));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_remove_target_credential(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* credential_id = nullptr;
            int authorization_fd = -1;
            const int read_result = sd_bus_message_read(
                message,
                "ssh",
                &profile_id,
                &credential_id,
                &authorization_fd
            );
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return reply_operational_json(message, error, "remove-target-credential", profile, [&] {
                return codec_.encode(credential_administration_.remove_credential(
                    caller_bus_name(message),
                    profile,
                    credential_id == nullptr ? "" : credential_id,
                    authorization_fd
                ));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_list_provisioning_devices(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            return reply_operational_json(message, error, "list-provisioning-devices", "", [&] {
                return codec_.encode(device_provisioning_.list_devices(caller_bus_name(message)));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_list_source_candidates(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            return reply_operational_json(message, error, "list-source-candidates", "", [&] {
                return config::json::dump_json(
                    device_provisioning_.list_source_candidates(caller_bus_name(message))
                );
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_start_device_preparation(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* request_json = nullptr;
            int passphrase_fd = -1;
            const int read_result = sd_bus_message_read(message, "sh", &request_json, &passphrase_fd);
            if (read_result < 0)
                return read_result;
            const auto request = config::json::Json::parse(request_json == nullptr ? "{}" : request_json);
            control::DevicePreparationRequest parsed{
                .profile_id = request.value("profileId", ""),
                .profile_name = request.value("profileName", ""),
                .candidate_id = request.value("candidateId", ""),
                .source_subvolume = request.value("sourceSubvolume", ""),
                .passphrase_label = request.value("passphraseLabel", ""),
                .create_automatic_key = request.value("createAutomaticKey", true),
            };
            const std::string profile = parsed.profile_id;
            return reply_operational_json(message, error, manager_protocol::feature::device_provisioning, profile, [&] {
                return codec_.encode(device_provisioning_.start(
                    caller_bus_name(message),
                    caller_uid(message),
                    parsed,
                    passphrase_fd
                ));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_get_device_preparation(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* operation_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &operation_id);
            if (read_result < 0)
                return read_result;
            const std::string operation = operation_id == nullptr ? "" : operation_id;
            return reply_operational_json(message, error, "get-device-preparation", "", [&] {
                return codec_.encode(device_provisioning_.status(
                    caller_bus_name(message),
                    caller_uid(message),
                    operation
                ));
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

int ManagerDbusObject::handle_cancel_device_preparation(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* operation_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &operation_id);
            if (read_result < 0)
                return read_result;
            const std::string operation = operation_id == nullptr ? "" : operation_id;
            return reply_operational_json(message, error, "cancel-device-preparation", "", [&] {
                device_provisioning_.cancel(caller_bus_name(message), caller_uid(message), operation);
                return config::json::dump_json({
                    {"schemaVersion", manager_protocol::operation_result_schema_version},
                    {"operation", "cancel-device-preparation"},
                    {"operationId", operation},
                    {"profileId", ""},
                    {"accepted", true},
                });
            });
        },
        [&](const std::exception* exception) { return set_callback_error(error, exception); }
    );
}

} // namespace btrfsbackup::daemon::dbus
