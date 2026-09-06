// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerDbusObject.hpp>

#include <systemd/sd-bus.h>

#include <core/ManagerProtocol.hpp>

namespace {

namespace manager_protocol = btrfsbackup::manager_protocol;
using ManagerDbusObject = btrfsbackup::daemon::dbus::ManagerDbusObject;

int get_capabilities(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->read_methods().get_capabilities(message, error);
}
int list_profiles(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->read_methods().list_profiles(message, error);
}
int get_status(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->read_methods().get_status(message, error);
}
int get_history_sanitized(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->read_methods().get_history_sanitized(message, error);
}
int get_device_state(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->read_methods().get_device_state(message, error);
}
int start_backup(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->operational_methods().start_backup(message, error);
}
int cancel_backup(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->operational_methods().cancel_backup(message, error);
}
int validate_target(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->operational_methods().validate_target(message, error);
}
int eject_target(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->operational_methods().eject_target(message, error);
}
int get_profile_details(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->profile_methods().get_profile_details(message, error);
}
int update_profile_settings(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->profile_methods().update_profile_settings(message, error);
}
int add_profile_source(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->profile_methods().add_profile_source(message, error);
}
int update_profile_source(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->profile_methods().update_profile_source(message, error);
}
int remove_profile_source(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->profile_methods().remove_profile_source(message, error);
}
int delete_profile(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->profile_methods().delete_profile(message, error);
}
int set_profile_enabled(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->profile_methods().set_profile_enabled(message, error);
}
int open_browse_session(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->browse_methods().open_browse_session(message, error);
}
int renew_browse_session(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->browse_methods().renew_browse_session(message, error);
}
int begin_browse_operation(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->browse_methods().begin_browse_operation(message, error);
}
int end_browse_operation(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->browse_methods().end_browse_operation(message, error);
}
int close_browse_session(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->browse_methods().close_browse_session(message, error);
}
int list_browse_directory(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->browse_methods().list_browse_directory(message, error);
}
int list_browse_directory_page(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->browse_methods().list_browse_directory_page(message, error);
}
int list_previous_versions(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->browse_methods().list_previous_versions(message, error);
}
int inspect_browse_entry(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->browse_methods().inspect_browse_entry(message, error);
}
int open_browse_file(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->browse_methods().open_browse_file(message, error);
}
int open_browse_entry(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->browse_methods().open_browse_entry(message, error);
}
int inspect_browse_repository(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->browse_methods().inspect_browse_repository(message, error);
}
int resolve_backup_coverage(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->browse_methods().resolve_backup_coverage(message, error);
}
int list_target_credentials(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->credential_methods().list_target_credentials(message, error);
}
int add_target_passphrase(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->credential_methods().add_target_passphrase(message, error);
}
int add_target_key(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->credential_methods().add_target_key(message, error);
}
int generate_target_key(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->credential_methods().generate_target_key(message, error);
}
int remove_target_credential(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->credential_methods().remove_target_credential(message, error);
}
int inspect_storage_topology(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->provisioning_methods().inspect_storage_topology(message, error);
}
int inspect_existing_target(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->provisioning_methods().inspect_existing_target(message, error);
}
int build_device_preparation_plan(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->provisioning_methods().build_device_preparation_plan(message, error);
}
int list_source_candidates(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->provisioning_methods().list_source_candidates(message, error);
}
int start_device_preparation(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->provisioning_methods().start_device_preparation(message, error);
}
int get_device_preparation(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->provisioning_methods().get_device_preparation(message, error);
}
int cancel_device_preparation(sd_bus_message* message, void* userdata, sd_bus_error* error) noexcept {
    return static_cast<ManagerDbusObject*>(userdata)->provisioning_methods().cancel_device_preparation(message, error);
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
    SD_BUS_METHOD(manager_protocol::method::renew_browse_session, "s", "s", renew_browse_session, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::begin_browse_operation, "s", "s", begin_browse_operation, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::end_browse_operation, "ss", "s", end_browse_operation, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::close_browse_session, "s", "s", close_browse_session, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::list_browse_directory, "ss", "s", list_browse_directory, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::list_browse_directory_page, "sssu", "s", list_browse_directory_page, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::list_previous_versions, "sssssu", "s", list_previous_versions, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::inspect_browse_entry, "ss", "s", inspect_browse_entry, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::open_browse_file, "ss", "h", open_browse_file, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::open_browse_entry, "ss", "h", open_browse_entry, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::inspect_browse_repository, "s", "s", inspect_browse_repository, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::resolve_backup_coverage, "s", "s", resolve_backup_coverage, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::list_target_credentials, "s", "s", list_target_credentials, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::add_target_passphrase, "shhs", "s", add_target_passphrase, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::add_target_key, "shhsb", "s", add_target_key, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::generate_target_key, "shsb", "s", generate_target_key, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::remove_target_credential, "ssh", "s", remove_target_credential, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::inspect_storage_topology, "", "s", inspect_storage_topology, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::inspect_existing_target, "sh", "s", inspect_existing_target, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(manager_protocol::method::build_device_preparation_plan, "s", "s", build_device_preparation_plan, SD_BUS_VTABLE_UNPRIVILEGED),
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
    : support_(audit_log),
      read_methods_(service, profile_administration, support_),
      operational_methods_(operational, support_),
      profile_methods_(profile_administration, support_),
      browse_methods_(service, browse_sessions, support_),
      credential_methods_(credential_administration, support_),
      provisioning_methods_(device_provisioning, support_) {
}

const sd_bus_vtable* ManagerDbusObject::vtable() noexcept {
    return manager_vtable;
}

ManagerReadMethods& ManagerDbusObject::read_methods() noexcept {
    return read_methods_;
}
ManagerOperationalMethods& ManagerDbusObject::operational_methods() noexcept {
    return operational_methods_;
}
ManagerProfileMethods& ManagerDbusObject::profile_methods() noexcept {
    return profile_methods_;
}
ManagerBrowseMethods& ManagerDbusObject::browse_methods() noexcept {
    return browse_methods_;
}
ManagerCredentialMethods& ManagerDbusObject::credential_methods() noexcept {
    return credential_methods_;
}
ManagerProvisioningMethods& ManagerDbusObject::provisioning_methods() noexcept {
    return provisioning_methods_;
}

} // namespace btrfsbackup::daemon::dbus
