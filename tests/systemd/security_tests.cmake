# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(variable IN ITEMS UNIT_FILE POLICY SYSTEMD_ANALYZE TEST_ROOT)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()

if(NOT EXISTS "${UNIT_FILE}")
    message(FATAL_ERROR "Unit file does not exist: ${UNIT_FILE}")
endif()

get_filename_component(unit_name "${UNIT_FILE}" NAME)
string(REGEX REPLACE "\\.example$" "" unit_name "${unit_name}")
set(test_dir "${TEST_ROOT}/systemd-security-${POLICY}")
set(staged_unit "${test_dir}/${unit_name}")

file(REMOVE_RECURSE "${test_dir}")
file(MAKE_DIRECTORY "${test_dir}")
file(READ "${UNIT_FILE}" unit_contents)
string(REPLACE "@BTRFSBACKUP_BACKUP_COMMAND@" "/usr/bin/true" unit_contents "${unit_contents}")
string(REPLACE "@BTRFSBACKUP_DEVICE_PREPARATION_EXECUTABLE@" "/usr/bin/true" unit_contents "${unit_contents}")
string(REPLACE "@BTRFSBACKUP_EJECT_SCRIPT_PATH@" "/usr/bin/true" unit_contents "${unit_contents}")
string(REPLACE "{{PROFILE_ID}}" "default" unit_contents "${unit_contents}")
file(WRITE "${staged_unit}" "${unit_contents}")

set(required_directives
    "NoNewPrivileges=yes"
    "PrivateTmp=yes"
    "ProtectKernelTunables=yes"
    "ProtectKernelModules=yes"
    "ProtectControlGroups=yes"
    "ProtectHostname=yes"
    "ProtectClock=yes"
    "ProtectProc=invisible"
    "LockPersonality=yes"
    "RestrictRealtime=yes"
    "MemoryDenyWriteExecute=yes"
    "SystemCallArchitectures=native"
    "Environment=PATH=/usr/bin"
    "RestrictAddressFamilies=AF_UNIX AF_NETLINK"
)

if("${POLICY}" STREQUAL "backup")
    set(threshold_tenths 80)
    list(APPEND required_directives "ProtectSystem=full")
elseif("${POLICY}" STREQUAL "device-preparation")
    set(threshold_tenths 45)
    list(APPEND required_directives
        "Wants=modprobe@dm_mod.service modprobe@dm_crypt.service"
        "After=systemd-udevd.service modprobe@dm_mod.service modprobe@dm_crypt.service"
        "User=root"
        "Group=root"
        "UMask=0077"
        "PrivateMounts=yes"
        "ProtectSystem=strict"
        "ProtectHome=read-only"
        "ProtectKernelLogs=yes"
        "RestrictNamespaces=yes"
        "DevicePolicy=closed"
        "CapabilityBoundingSet=CAP_SYS_ADMIN CAP_DAC_OVERRIDE CAP_FOWNER"
    )
else()
    message(FATAL_ERROR "Unknown systemd security policy: ${POLICY}")
endif()

foreach(directive IN LISTS required_directives)
    string(FIND "\n${unit_contents}" "\n${directive}\n" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing ${POLICY} systemd hardening directive: ${directive}")
    endif()
endforeach()

if("${POLICY}" STREQUAL "device-preparation")
    string(FIND "\n${unit_contents}" "\nDeviceAllow=block-* rw\n" broad_device_access)
    if(NOT broad_device_access EQUAL -1)
        message(FATAL_ERROR "Device preparation must not grant access to every block device")
    endif()
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "SYSTEMD_UNIT_PATH=${test_dir}"
        "${SYSTEMD_ANALYZE}" security
        --offline=yes
        --instance=default
        --threshold=100
        --no-pager
        "${unit_name}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR
        "${POLICY} systemd security analysis failed\n"
        "stdout:\n${output}\n"
        "stderr:\n${error}"
    )
endif()
set(security_output "${output}${error}")
string(REGEX MATCH "Overall exposure level[^:\n]*: ([0-9]+)\\.([0-9])" exposure_match "${security_output}")
if(exposure_match STREQUAL "")
    message(FATAL_ERROR "systemd security analysis did not report an exposure level")
endif()
math(EXPR exposure_tenths "${CMAKE_MATCH_1} * 10 + ${CMAKE_MATCH_2}")
if(exposure_tenths GREATER threshold_tenths)
    message(FATAL_ERROR
        "${POLICY} systemd exposure ${CMAKE_MATCH_1}.${CMAKE_MATCH_2} exceeds accepted threshold"
    )
endif()

message(STATUS "${output}")
