# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

file(READ "${PROJECT_SOURCE_DIR}/data/dbus/io.github.btrfsbackup.Manager1.xml" manager_xml)
file(READ "${PROJECT_SOURCE_DIR}/data/dbus/io.github.btrfsbackup.Manager1.conf" manager_bus_policy)
file(READ "${PROJECT_SOURCE_DIR}/data/polkit/io.github.btrfsbackup.policy" manager_polkit_policy)
file(READ "${PROJECT_SOURCE_DIR}/src/core/ManagerProtocol.hpp" manager_protocol)
file(READ "${PROJECT_SOURCE_DIR}/src/daemon/control/OperationalControlService.cpp" manager_authorization_map)
file(READ "${PROJECT_SOURCE_DIR}/src/daemon/dbus/ManagerDbusObject.cpp" manager_vtable)
file(READ "${PROJECT_SOURCE_DIR}/src/daemon/dbus/ManagerProvisioningMethods.cpp" manager_provisioning_methods)
file(READ "${PROJECT_SOURCE_DIR}/src/daemon/dbus/ManagerJsonCodec.cpp" manager_json_codec)
file(READ "${PROJECT_SOURCE_DIR}/integrations/kde/kcm/DeviceProvisioningModel.cpp" provisioning_kcm)
file(READ "${PROJECT_SOURCE_DIR}/integrations/kde/kcm/ProfileConfigurationModel.cpp" profile_configuration_kcm)
file(READ "${PROJECT_SOURCE_DIR}/integrations/kde/kcm/TargetCredentialModel.cpp" target_credentials_kcm)
file(READ "${PROJECT_SOURCE_DIR}/integrations/kde/kcm/ui/ProfileOverview.qml" profile_overview_kcm)
file(READ "${PROJECT_SOURCE_DIR}/integrations/kde/models/ProfilePresentation.qml" profile_presentation)
file(READ "${PROJECT_SOURCE_DIR}/integrations/kde/models/BackupStatusModel.cpp" plasma_status_model)
file(GLOB_RECURSE kde_dbus_sources
    "${PROJECT_SOURCE_DIR}/integrations/kde/*.cpp"
    "${PROJECT_SOURCE_DIR}/integrations/kde/*.hpp"
    "${PROJECT_SOURCE_DIR}/integrations/kde/*.qml"
)
list(FILTER kde_dbus_sources EXCLUDE REGEX "/tests/")
set(kde_dbus_contract)
foreach(source IN LISTS kde_dbus_sources)
    file(READ "${source}" content)
    string(APPEND kde_dbus_contract "${content}\n")
endforeach()
string(REGEX REPLACE "[ \t\r\n]+" "" compact_xml "${manager_xml}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_bus_policy "${manager_bus_policy}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_polkit_policy "${manager_polkit_policy}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_authorization_map "${manager_authorization_map}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_manager_vtable "${manager_vtable}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_manager_json_codec "${manager_json_codec}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_provisioning_kcm "${provisioning_kcm}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_profile_overview_kcm "${profile_overview_kcm}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_profile_presentation "${profile_presentation}")

function(assert_contains content fragment description)
    string(FIND "${content}" "${fragment}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "manager D-Bus contract is missing ${description}")
    endif()
endfunction()

function(assert_not_contains content fragment description)
    string(FIND "${content}" "${fragment}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "manager D-Bus contract still contains ${description}")
    endif()
endfunction()

function(assert_same_set actual_variable expected_variable description)
    set(actual ${${actual_variable}})
    set(expected ${${expected_variable}})
    list(REMOVE_DUPLICATES actual)
    list(REMOVE_DUPLICATES expected)
    list(SORT actual)
    list(SORT expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "manager D-Bus contract has inconsistent ${description}\n"
            "actual: ${actual}\n"
            "expected: ${expected}"
        )
    endif()
endfunction()

function(assert_authorized_method identifier name input_signature output_signature xml_fragment action action_id)
    assert_method(${identifier} ${name} "${input_signature}" "${output_signature}" "${xml_fragment}")
    set_property(GLOBAL APPEND PROPERTY manager_authorized_method_ids "${identifier}")
    set_property(GLOBAL APPEND PROPERTY manager_authorization_action_ids "${action_id}")
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

function(assert_unprivileged_method identifier name input_signature output_signature xml_fragment)
    assert_method(${identifier} ${name} "${input_signature}" "${output_signature}" "${xml_fragment}")
    set_property(GLOBAL APPEND PROPERTY manager_unprivileged_method_ids "${identifier}")
    assert_not_contains(
        "${compact_authorization_map}"
        "{manager_protocol::method::${identifier},"
        "a polkit mapping for read-only method ${name}"
    )
endfunction()

function(assert_method identifier name input_signature output_signature xml_fragment)
    set_property(GLOBAL APPEND PROPERTY manager_contract_method_ids "${identifier}")
    set_property(GLOBAL APPEND PROPERTY manager_contract_method_names "${name}")
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
    set_property(GLOBAL APPEND PROPERTY manager_contract_signal_ids "${identifier}")
    set_property(GLOBAL APPEND PROPERTY manager_contract_signal_names "${name}")
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

function(assert_delegated_methods group)
    foreach(method IN LISTS ARGN)
        assert_contains(
            "${compact_manager_vtable}"
            "->${group}().${method}(message,error);"
            "the ${method} delegation to ${group}"
        )
    endforeach()
endfunction()

assert_not_contains(
    "${manager_vtable}"
    "sd_bus_message_read"
    "D-Bus argument parsing in the vtable owner"
)
assert_delegated_methods(
    read_methods
    get_capabilities list_profiles get_status get_history_sanitized get_device_state
)
assert_delegated_methods(
    operational_methods
    start_backup cancel_backup validate_target eject_target
)
assert_delegated_methods(
    profile_methods
    get_profile_details update_profile_settings add_profile_source update_profile_source
    remove_profile_source delete_profile set_profile_enabled
)
assert_delegated_methods(
    browse_methods
    open_browse_session renew_browse_session set_browse_session_active close_browse_session
    list_browse_directory inspect_browse_entry open_browse_file resolve_backup_coverage
)
assert_delegated_methods(
    credential_methods
    list_target_credentials add_target_passphrase add_target_key generate_target_key
    remove_target_credential
)
assert_delegated_methods(
    provisioning_methods
    list_source_candidates start_device_preparation
    get_device_preparation cancel_device_preparation
)

assert_contains(
    "${compact_xml}"
    "<nodename=\"/io/github/btrfsbackup/Manager1\"><interfacename=\"io.github.btrfsbackup.Manager1\">"
    "the manager object path and interface"
)

assert_unprivileged_method(
    get_capabilities GetCapabilities "" s
    "<methodname=\"GetCapabilities\"><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    list_profiles ListProfiles "" s
    "<methodname=\"ListProfiles\"><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    get_status GetStatus s s
    "<methodname=\"GetStatus\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    get_history_sanitized GetHistorySanitized suu s
    "<methodname=\"GetHistorySanitized\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"offset\"type=\"u\"direction=\"in\"/><argname=\"limit\"type=\"u\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    get_device_state GetDeviceState s s
    "<methodname=\"GetDeviceState\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_authorized_method(
    start_backup StartBackup s s
    "<methodname=\"StartBackup\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    StartBackup io.github.btrfsbackup.start-backup
)
assert_authorized_method(
    cancel_backup CancelBackup ss s
    "<methodname=\"CancelBackup\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"runId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    CancelBackup io.github.btrfsbackup.cancel-backup
)
assert_authorized_method(
    validate_target ValidateTarget s s
    "<methodname=\"ValidateTarget\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    ValidateTarget io.github.btrfsbackup.validate-target
)
assert_authorized_method(
    eject_target EjectTarget s s
    "<methodname=\"EjectTarget\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    EjectTarget io.github.btrfsbackup.eject-target
)
assert_unprivileged_method(
    get_profile_details GetProfileDetails s s
    "<methodname=\"GetProfileDetails\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_authorized_method(
    update_profile_settings UpdateProfileSettings ssss s
    "<methodname=\"UpdateProfileSettings\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"request\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    ManageProfileConfiguration io.github.btrfsbackup.manage-profile-configuration
)
assert_authorized_method(
    add_profile_source AddProfileSource ssss s
    "<methodname=\"AddProfileSource\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"request\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    ManageProfileConfiguration io.github.btrfsbackup.manage-profile-configuration
)
assert_authorized_method(
    update_profile_source UpdateProfileSource sssss s
    "<methodname=\"UpdateProfileSource\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"sourceId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"request\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    ManageProfileConfiguration io.github.btrfsbackup.manage-profile-configuration
)
assert_authorized_method(
    remove_profile_source RemoveProfileSource ssss s
    "<methodname=\"RemoveProfileSource\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"sourceId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    ManageProfileConfiguration io.github.btrfsbackup.manage-profile-configuration
)
assert_authorized_method(
    delete_profile DeleteProfile sss s
    "<methodname=\"DeleteProfile\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    DeleteProfileConfiguration io.github.btrfsbackup.delete-profile-configuration
)
assert_authorized_method(
    set_profile_enabled SetProfileEnabled sb s
    "<methodname=\"SetProfileEnabled\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"enabled\"type=\"b\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    SetProfileEnabled io.github.btrfsbackup.set-profile-enabled
)
assert_authorized_method(
    open_browse_session OpenBrowseSession s s
    "<methodname=\"OpenBrowseSession\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
    OpenBrowseSession io.github.btrfsbackup.open-browse-session
)
assert_unprivileged_method(
    renew_browse_session RenewBrowseSession s s
    "<methodname=\"RenewBrowseSession\"><argname=\"sessionId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    set_browse_session_active SetBrowseSessionActive sb s
    "<methodname=\"SetBrowseSessionActive\"><argname=\"sessionId\"type=\"s\"direction=\"in\"/><argname=\"active\"type=\"b\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    close_browse_session CloseBrowseSession s s
    "<methodname=\"CloseBrowseSession\"><argname=\"sessionId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    list_browse_directory ListBrowseDirectory ss s
    "<methodname=\"ListBrowseDirectory\"><argname=\"sessionId\"type=\"s\"direction=\"in\"/><argname=\"relativePath\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    inspect_browse_entry InspectBrowseEntry ss s
    "<methodname=\"InspectBrowseEntry\"><argname=\"sessionId\"type=\"s\"direction=\"in\"/><argname=\"relativePath\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    open_browse_file OpenBrowseFile ss h
    "<methodname=\"OpenBrowseFile\"><argname=\"sessionId\"type=\"s\"direction=\"in\"/><argname=\"relativePath\"type=\"s\"direction=\"in\"/><argname=\"file\"type=\"h\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    open_browse_root OpenBrowseRoot s h
    "<methodname=\"OpenBrowseRoot\"><argname=\"sessionId\"type=\"s\"direction=\"in\"/><argname=\"root\"type=\"h\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    inspect_browse_repository InspectBrowseRepository s s
    "<methodname=\"InspectBrowseRepository\"><argname=\"sessionId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    resolve_backup_coverage ResolveBackupCoverage s s
    "<methodname=\"ResolveBackupCoverage\"><argname=\"localPath\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    list_target_credentials ListTargetCredentials s s
    "<methodname=\"ListTargetCredentials\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
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
assert_unprivileged_method(
    inspect_storage_topology InspectStorageTopology "" s
    "<methodname=\"InspectStorageTopology\"><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    inspect_existing_target InspectExistingTarget sh s
    "<methodname=\"InspectExistingTarget\"><argname=\"request\"type=\"s\"direction=\"in\"/><argname=\"credential\"type=\"h\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    build_device_preparation_plan BuildDevicePreparationPlan s s
    "<methodname=\"BuildDevicePreparationPlan\"><argname=\"request\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_unprivileged_method(
    list_source_candidates ListSourceCandidates "" s
    "<methodname=\"ListSourceCandidates\"><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
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

assert_contains(
    "${manager_protocol}"
    "inline constexpr int device_provisioning_schema_version = 2;"
    "the device preparation status schema version"
)
assert_contains(
    "${manager_provisioning_methods}"
    ".plan_id = request.value(\"planId\", \"\")"
    "the daemon provisioning plan request"
)
assert_contains(
    "${compact_provisioning_kcm}"
    "{QStringLiteral(\"planId\"),plan_id}"
    "the KCM provisioning plan request"
)
assert_contains(
    "${manager_provisioning_methods}"
    ".source_candidate_id = request.value(\"sourceCandidateId\", \"\")"
    "the daemon caller-bound provisioning source request"
)
assert_contains(
    "${compact_provisioning_kcm}"
    "{QStringLiteral(\"sourceCandidateId\"),source_candidate_id.trimmed()}"
    "the KCM caller-bound provisioning source request"
)
assert_not_contains(
    "${manager_provisioning_methods}${provisioning_kcm}"
    "request.value(\"sourceSubvolume\""
    "a client-provided provisioning source path"
)
foreach(legacy_field IN ITEMS devicePath expectedSerial expectedSizeBytes)
    assert_not_contains("${manager_provisioning_methods}" "${legacy_field}" "legacy daemon field ${legacy_field}")
    assert_not_contains("${provisioning_kcm}" "${legacy_field}" "legacy KCM field ${legacy_field}")
endforeach()

assert_contains(
    "${target_credentials_kcm}"
    "ManagerEventSubscriber::deviceStateChanged"
    "credential refresh after a device state change"
)
assert_not_contains(
    "${provisioning_kcm}${profile_configuration_kcm}${target_credentials_kcm}${plasma_status_model}"
    "reply.error().message()"
    "raw untranslated manager errors in KDE clients"
)
assert_contains(
    "${compact_profile_overview_kcm}"
    "enabled:BtrfsBackup.ProfilePresentation.canEject(root.profileStatus)"
    "the shared eject action guard"
)
assert_contains(
    "${compact_profile_presentation}"
    "&&((profileStatus?.target?.mounted??false)||(profileStatus?.target?.unlocked??false))"
    "the eject action guard for a locked target"
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

get_property(contract_method_ids GLOBAL PROPERTY manager_contract_method_ids)
get_property(contract_method_names GLOBAL PROPERTY manager_contract_method_names)
get_property(contract_signal_ids GLOBAL PROPERTY manager_contract_signal_ids)
get_property(contract_signal_names GLOBAL PROPERTY manager_contract_signal_names)
get_property(authorized_method_ids GLOBAL PROPERTY manager_authorized_method_ids)
get_property(unprivileged_method_ids GLOBAL PROPERTY manager_unprivileged_method_ids)
get_property(contract_action_ids GLOBAL PROPERTY manager_authorization_action_ids)
set(classified_method_ids ${authorized_method_ids} ${unprivileged_method_ids})
assert_same_set(classified_method_ids contract_method_ids "method authorization classifications")

string(REGEX MATCHALL
    "SD_BUS_METHOD\\(manager_protocol::method::[a-z0-9_]+"
    vtable_method_entries
    "${manager_vtable}"
)
set(vtable_method_ids)
foreach(entry IN LISTS vtable_method_entries)
    string(REGEX REPLACE ".*method::([a-z0-9_]+)" "\\1" identifier "${entry}")
    list(APPEND vtable_method_ids "${identifier}")
endforeach()
assert_same_set(vtable_method_ids contract_method_ids "daemon vtable methods")

string(REGEX MATCHALL "<methodname=\"[A-Za-z0-9]+\"" xml_method_entries "${compact_xml}")
set(xml_method_names)
foreach(entry IN LISTS xml_method_entries)
    string(REGEX REPLACE ".*=\"([A-Za-z0-9]+)\"" "\\1" name "${entry}")
    list(APPEND xml_method_names "${name}")
endforeach()
assert_same_set(xml_method_names contract_method_names "XML methods")

string(REGEX MATCHALL
    "send_interface=\"io.github.btrfsbackup.Manager1\"send_member=\"[A-Za-z0-9]+\""
    bus_policy_method_entries
    "${compact_bus_policy}"
)
set(bus_policy_method_names)
foreach(entry IN LISTS bus_policy_method_entries)
    string(REGEX REPLACE ".*send_member=\"([A-Za-z0-9]+)\"" "\\1" name "${entry}")
    list(APPEND bus_policy_method_names "${name}")
endforeach()
assert_same_set(bus_policy_method_names contract_method_names "system bus policy methods")

string(REGEX MATCHALL
    "\\{manager_protocol::method::[a-z0-9_]+,ManagerAuthorizationAction::"
    authorization_method_entries
    "${compact_authorization_map}"
)
set(authorization_method_ids)
foreach(entry IN LISTS authorization_method_entries)
    string(REGEX REPLACE ".*method::([a-z0-9_]+),.*" "\\1" identifier "${entry}")
    list(APPEND authorization_method_ids "${identifier}")
endforeach()
assert_same_set(authorization_method_ids authorized_method_ids "shared authorization mappings")

string(REGEX MATCHALL "<actionid=\"[a-z0-9.-]+\"" polkit_action_entries "${compact_polkit_policy}")
set(polkit_action_ids)
foreach(entry IN LISTS polkit_action_entries)
    string(REGEX REPLACE ".*=\"([a-z0-9.-]+)\"" "\\1" action_id "${entry}")
    list(APPEND polkit_action_ids "${action_id}")
endforeach()
string(REGEX MATCHALL
    "caseManagerAuthorizationAction::[A-Za-z]+:return\"[a-z0-9.-]+\";"
    mapped_action_entries
    "${compact_authorization_map}"
)
set(mapped_action_ids)
foreach(entry IN LISTS mapped_action_entries)
    string(REGEX REPLACE ".*return\"" "" action_id "${entry}")
    string(REGEX REPLACE "\".*" "" action_id "${action_id}")
    list(APPEND mapped_action_ids "${action_id}")
endforeach()
assert_same_set(polkit_action_ids mapped_action_ids "installed and mapped polkit actions")
assert_same_set(polkit_action_ids contract_action_ids "method and polkit action mappings")

string(REGEX MATCHALL
    "manager_protocol::method::[a-z0-9_]+"
    kde_method_entries
    "${kde_dbus_contract}"
)
set(kde_method_ids)
foreach(entry IN LISTS kde_method_entries)
    string(REGEX REPLACE ".*method::([a-z0-9_]+)" "\\1" identifier "${entry}")
    list(APPEND kde_method_ids "${identifier}")
endforeach()
assert_same_set(kde_method_ids contract_method_ids "KDE client methods")

string(REGEX MATCHALL
    "SD_BUS_SIGNAL\\(manager_protocol::signal::[a-z0-9_]+"
    vtable_signal_entries
    "${manager_vtable}"
)
set(vtable_signal_ids)
foreach(entry IN LISTS vtable_signal_entries)
    string(REGEX REPLACE ".*signal::([a-z0-9_]+)" "\\1" identifier "${entry}")
    list(APPEND vtable_signal_ids "${identifier}")
endforeach()
assert_same_set(vtable_signal_ids contract_signal_ids "daemon vtable signals")

string(REGEX MATCHALL "<signalname=\"[A-Za-z0-9]+\"" xml_signal_entries "${compact_xml}")
set(xml_signal_names)
foreach(entry IN LISTS xml_signal_entries)
    string(REGEX REPLACE ".*=\"([A-Za-z0-9]+)\"" "\\1" name "${entry}")
    list(APPEND xml_signal_names "${name}")
endforeach()
assert_same_set(xml_signal_names contract_signal_names "XML signals")

string(REGEX MATCHALL
    "IoGithubBtrfsbackupManager1Interface::[A-Z][A-Za-z0-9]+"
    kde_signal_entries
    "${kde_dbus_contract}"
)
set(kde_signal_names)
foreach(entry IN LISTS kde_signal_entries)
    string(REGEX REPLACE ".*Interface::([A-Za-z0-9]+)" "\\1" name "${entry}")
    list(APPEND kde_signal_names "${name}")
endforeach()
assert_same_set(kde_signal_names contract_signal_names "KDE client signals")

string(REGEX MATCHALL
    "inline constexpr char [a-z0-9_]+\\[\\] = \"[A-Z][A-Za-z0-9]+\";"
    protocol_member_entries
    "${manager_protocol}"
)
set(protocol_member_names)
foreach(entry IN LISTS protocol_member_entries)
    string(REGEX REPLACE ".*= \"" "" name "${entry}")
    string(REGEX REPLACE "\".*" "" name "${name}")
    list(APPEND protocol_member_names "${name}")
endforeach()
set(contract_member_names ${contract_method_names} ${contract_signal_names})
assert_same_set(protocol_member_names contract_member_names "protocol constants")
