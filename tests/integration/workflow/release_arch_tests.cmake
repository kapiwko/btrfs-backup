# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(variable IN ITEMS CMAKE_COMMAND PYTHON SOURCE_DIR TEST_ROOT RELEASE_BUILD_DIR)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()

set(root "${TEST_ROOT}/release-arch")
file(REMOVE_RECURSE "${root}")
function(build_arch destination)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env SOURCE_DATE_EPOCH=1700000000 BUILD_JOBS=2
            "${PYTHON}" "${SOURCE_DIR}/tools/release.py" --target arch-base
            --dist-dir "${destination}" --build-dir "${RELEASE_BUILD_DIR}" --skip-tests
        RESULT_VARIABLE result ERROR_VARIABLE error OUTPUT_QUIET
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Arch release failed: ${error}")
    endif()
endfunction()

build_arch("${root}/first")
build_arch("${root}/second")
file(GLOB first_packages "${root}/first/*.pkg.tar.zst")
file(GLOB second_packages "${root}/second/*.pkg.tar.zst")
list(LENGTH first_packages first_count)
list(LENGTH second_packages second_count)
if(NOT first_count EQUAL 1 OR NOT second_count EQUAL 1)
    message(FATAL_ERROR "Expected one Arch base package per build")
endif()
file(SHA256 "${first_packages}" first_hash)
file(SHA256 "${second_packages}" second_hash)
if(NOT first_hash STREQUAL second_hash)
    message(FATAL_ERROR "Arch base package is not reproducible")
endif()

execute_process(
    COMMAND bsdtar -tf "${first_packages}"
    OUTPUT_VARIABLE entries RESULT_VARIABLE list_result
)
if(NOT list_result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect Arch base package")
endif()
foreach(expected IN ITEMS
        ".MTREE" ".PKGINFO" "etc/btrfs-backup/hooks.d/"
        "usr/bin/btrfs-backup" "usr/bin/btrfs-backupctl" "usr/bin/btrfs-backupd"
        "usr/bin/btrfs-backup-device-preparation"
        "usr/share/libalpm/hooks/90-btrfs-backup-v4-migration.hook"
        "usr/lib/systemd/system/btrfs-backup@.service"
        "usr/lib/tmpfiles.d/btrfs-backup.conf")
    string(FIND "${entries}" "${expected}\n" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Arch base package is missing ${expected}")
    endif()
endforeach()
string(FIND "${entries}" ".INSTALL\n" install_scriptlet)
if(NOT install_scriptlet EQUAL -1)
    message(FATAL_ERROR "Arch base package still contains the ineffective pre_upgrade scriptlet")
endif()
execute_process(
    COMMAND bsdtar -xOf "${first_packages}" usr/share/libalpm/hooks/90-btrfs-backup-v4-migration.hook
    OUTPUT_VARIABLE hook_content
    RESULT_VARIABLE hook_result
)
if(NOT hook_result EQUAL 0 OR
   NOT hook_content MATCHES "Operation = Upgrade" OR
   NOT hook_content MATCHES "Type = Package" OR
   NOT hook_content MATCHES "Target = btrfs-backup" OR
   NOT hook_content MATCHES "When = PreTransaction" OR
   NOT hook_content MATCHES "Exec = /usr/bin/btrfs-backupctl upgrade preflight" OR
   NOT hook_content MATCHES "AbortOnFail")
    message(FATAL_ERROR "Arch package is missing the fail-closed migration hook contract")
endif()
