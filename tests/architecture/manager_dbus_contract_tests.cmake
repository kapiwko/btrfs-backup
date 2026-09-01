# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

file(READ "${PROJECT_SOURCE_DIR}/data/dbus/io.github.btrfsbackup.Manager1.xml" manager_xml)
file(READ "${PROJECT_SOURCE_DIR}/data/dbus/io.github.btrfsbackup.Manager1.conf" manager_bus_policy)
file(READ "${PROJECT_SOURCE_DIR}/data/polkit/io.github.btrfsbackup.policy" manager_polkit_policy)
file(READ "${PROJECT_SOURCE_DIR}/src/core/ManagerProtocol.hpp" manager_protocol)
file(READ "${PROJECT_SOURCE_DIR}/src/daemon/control/OperationalControlService.cpp" manager_authorization_map)
file(READ "${PROJECT_SOURCE_DIR}/src/daemon/dbus/ManagerDbusObject.cpp" manager_vtable)
string(REGEX REPLACE "[ \t\r\n]+" "" compact_xml "${manager_xml}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_bus_policy "${manager_bus_policy}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_polkit_policy "${manager_polkit_policy}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_authorization_map "${manager_authorization_map}")

function(assert_contains content fragment description)
    string(FIND "${content}" "${fragment}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "manager D-Bus contract is missing ${description}")
    endif()
endfunction()

function(assert_authorized_method identifier name input_signature output_signature xml_fragment action action_id)
    assert_method(${identifier} ${name} "${input_signature}" "${output_signature}" "${xml_fragment}")
    assert_contains(
        "${compact_authorization_map}"
        "{manager_protocol::method::${identifier},ManagerAuthorizationAction::${action}}"
        "the ${name} authorization mapping"
    )
    assert_contains(
        "${compact_authorization_map}"
        "caseManagerAuthorizationAction::${action}:return\"${action_id}\";"
        "the ${name} polkit action identifier mapping"
    )
    assert_contains(
        "${compact_polkit_policy}"
        "<actionid=\"${action_id}\">"
        "the ${name} installed polkit action"
    )
endfunction()

function(assert_method identifier name input_signature output_signature xml_fragment)
    assert_contains(
        "${manager_protocol}"
        "inline constexpr char ${identifier}[] = \"${name}\";"
        "the ${name} protocol constant"
    )
    assert_contains(
        "${manager_vtable}"
        "SD_BUS_METHOD(manager_protocol::method::${identifier}, \"${input_signature}\", \"${output_signature}\""
        "the ${name} daemon vtable entry"
    )
    assert_contains("${compact_xml}" "${xml_fragment}" "the ${name} XML declaration")
    assert_contains(
        "${compact_bus_policy}"
        "send_interface=\"io.github.btrfsbackup.Manager1\"send_member=\"${name}\""
        "the ${name} system bus allow rule"
    )
endfunction()

function(assert_signal identifier name signature xml_fragment)
    assert_contains(
        "${manager_protocol}"
        "inline constexpr char ${identifier}[] = \"${name}\";"
        "the ${name} protocol constant"
    )
    assert_contains(
        "${manager_vtable}"
        "SD_BUS_SIGNAL(manager_protocol::signal::${identifier}, \"${signature}\", 0)"
        "the ${name} daemon vtable entry"
    )
    assert_contains("${compact_xml}" "${xml_fragment}" "the ${name} XML declaration")
endfunction()

assert_contains(
    "${compact_xml}"
    "<nodename=\"/io/github/btrfsbackup/Manager1\"><interfacename=\"io.github.btrfsbackup.Manager1\">"
    "the manager object path and interface"
)

assert_method(
    get_capabilities GetCapabilities "" s
    "<methodname=\"GetCapabilities\"><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    list_profiles ListProfiles "" s
    "<methodname=\"ListProfiles\"><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    get_status GetStatus s s
    "<methodname=\"GetStatus\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    get_history_sanitized GetHistorySanitized suu s
    "<methodname=\"GetHistorySanitized\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"offset\"type=\"u\"direction=\"in\"/><argname=\"limit\"type=\"u\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    get_device_state GetDeviceState s s
    "<methodname=\"GetDeviceState\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    start_backup StartBackup s s
    "<methodname=\"StartBackup\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    cancel_backup CancelBackup ss s
    "<methodname=\"CancelBackup\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"runId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    validate_target ValidateTarget s s
    "<methodname=\"ValidateTarget\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    eject_target EjectTarget s s
    "<methodname=\"EjectTarget\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    get_profile_details GetProfileDetails s s
    "<methodname=\"GetProfileDetails\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    update_profile_settings UpdateProfileSettings ssss s
    "<methodname=\"UpdateProfileSettings\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"request\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    add_profile_source AddProfileSource ssss s
    "<methodname=\"AddProfileSource\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"request\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    update_profile_source UpdateProfileSource sssss s
    "<methodname=\"UpdateProfileSource\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"sourceId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"request\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    remove_profile_source RemoveProfileSource ssss s
    "<methodname=\"RemoveProfileSource\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"sourceId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    delete_profile DeleteProfile sss s
    "<methodname=\"DeleteProfile\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    set_profile_enabled SetProfileEnabled sb s
    "<methodname=\"SetProfileEnabled\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"enabled\"type=\"b\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    open_browse_session OpenBrowseSession s s
    "<methodname=\"OpenBrowseSession\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    close_browse_session CloseBrowseSession s s
    "<methodname=\"CloseBrowseSession\"><argname=\"sessionId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    resolve_backup_coverage ResolveBackupCoverage s s
    "<methodname=\"ResolveBackupCoverage\"><argname=\"localPath\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_authorized_method(
    list_target_credentials ListTargetCredentials s s
    "<methodname=\"ListTargetCredentials\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    ManageTargetCredentials io.github.btrfsbackup.manage-target-credentials
)
assert_authorized_method(
    add_target_passphrase AddTargetPassphrase shhs s
    "<methodname=\"AddTargetPassphrase\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"authorizationSecret\"type=\"h\"direction=\"in\"/><argname=\"newSecret\"type=\"h\"direction=\"in\"/><argname=\"label\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    ManageTargetCredentials io.github.btrfsbackup.manage-target-credentials
)
assert_authorized_method(
    add_target_key AddTargetKey shhsb s
    "<methodname=\"AddTargetKey\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"authorizationSecret\"type=\"h\"direction=\"in\"/><argname=\"key\"type=\"h\"direction=\"in\"/><argname=\"label\"type=\"s\"direction=\"in\"/><argname=\"automatic\"type=\"b\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    ManageTargetCredentials io.github.btrfsbackup.manage-target-credentials
)
assert_authorized_method(
    generate_target_key GenerateTargetKey shsb s
    "<methodname=\"GenerateTargetKey\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"authorizationSecret\"type=\"h\"direction=\"in\"/><argname=\"label\"type=\"s\"direction=\"in\"/><argname=\"automatic\"type=\"b\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    ManageTargetCredentials io.github.btrfsbackup.manage-target-credentials
)
assert_authorized_method(
    remove_target_credential RemoveTargetCredential ssh s
    "<methodname=\"RemoveTargetCredential\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"credentialId\"type=\"s\"direction=\"in\"/><argname=\"authorizationSecret\"type=\"h\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    ManageTargetCredentials io.github.btrfsbackup.manage-target-credentials
)
assert_authorized_method(
    list_provisioning_devices ListProvisioningDevices "" s
    "<methodname=\"ListProvisioningDevices\"><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    PrepareBackupDevice io.github.btrfsbackup.prepare-backup-device
)
assert_authorized_method(
    list_source_candidates ListSourceCandidates "" s
    "<methodname=\"ListSourceCandidates\"><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    PrepareBackupDevice io.github.btrfsbackup.prepare-backup-device
)
assert_authorized_method(
    start_device_preparation StartDevicePreparation sh s
    "<methodname=\"StartDevicePreparation\"><argname=\"request\"type=\"s\"direction=\"in\"/><argname=\"passphrase\"type=\"h\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    PrepareBackupDevice io.github.btrfsbackup.prepare-backup-device
)
assert_authorized_method(
    get_device_preparation GetDevicePreparation s s
    "<methodname=\"GetDevicePreparation\"><argname=\"operationId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    PrepareBackupDevice io.github.btrfsbackup.prepare-backup-device
)
assert_authorized_method(
    cancel_device_preparation CancelDevicePreparation s s
    "<methodname=\"CancelDevicePreparation\"><argname=\"operationId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    PrepareBackupDevice io.github.btrfsbackup.prepare-backup-device
)

assert_signal(profiles_changed ProfilesChanged "" "<signalname=\"ProfilesChanged\"/>")
assert_signal(
    status_changed StatusChanged s
    "<signalname=\"StatusChanged\"><argname=\"profileId\"type=\"s\"/></signal>"
)
assert_signal(
    history_changed HistoryChanged s
    "<signalname=\"HistoryChanged\"><argname=\"profileId\"type=\"s\"/></signal>"
)
assert_signal(
    device_state_changed DeviceStateChanged s
    "<signalname=\"DeviceStateChanged\"><argname=\"profileId\"type=\"s\"/></signal>"
)

string(REGEX MATCHALL "<method name=" xml_methods "${manager_xml}")
list(LENGTH xml_methods method_count)
if(NOT method_count EQUAL 29)
    message(FATAL_ERROR "manager XML must declare exactly 29 methods, found ${method_count}")
endif()

string(REGEX MATCHALL "<signal name=" xml_signals "${manager_xml}")
list(LENGTH xml_signals signal_count)
if(NOT signal_count EQUAL 4)
    message(FATAL_ERROR "manager XML must declare exactly 4 signals, found ${signal_count}")
endif()
