# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

file(READ "${PROJECT_SOURCE_DIR}/data/dbus/io.github.btrfsbackup.Manager1.xml" manager_xml)
file(READ "${PROJECT_SOURCE_DIR}/src/core/ManagerProtocol.hpp" manager_protocol)
file(READ "${PROJECT_SOURCE_DIR}/src/daemon/dbus/ManagerDbusObject.cpp" manager_vtable)
string(REGEX REPLACE "[ \t\r\n]+" "" compact_xml "${manager_xml}")

function(assert_contains content fragment description)
    string(FIND "${content}" "${fragment}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "manager D-Bus contract is missing ${description}")
    endif()
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
    get_profile_for_editing GetProfileForEditing s s
    "<methodname=\"GetProfileForEditing\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    validate_profile_draft ValidateProfileDraft ssss s
    "<methodname=\"ValidateProfileDraft\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"document\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    save_profile SaveProfile ssss s
    "<methodname=\"SaveProfile\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"document\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    save_profile_hooks SaveProfileHooks ssss s
    "<methodname=\"SaveProfileHooks\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"document\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
)
assert_method(
    delete_profile DeleteProfile sss s
    "<methodname=\"DeleteProfile\"><argname=\"profileId\"type=\"s\"direction=\"in\"/><argname=\"expectedGeneration\"type=\"s\"direction=\"in\"/><argname=\"expectedFingerprint\"type=\"s\"direction=\"in\"/><argname=\"payload\"type=\"s\"direction=\"out\"/></method>"
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
if(NOT method_count EQUAL 14)
    message(FATAL_ERROR "manager XML must declare exactly 14 methods, found ${method_count}")
endif()

string(REGEX MATCHALL "<signal name=" xml_signals "${manager_xml}")
list(LENGTH xml_signals signal_count)
if(NOT signal_count EQUAL 4)
    message(FATAL_ERROR "manager XML must declare exactly 4 signals, found ${signal_count}")
endif()
