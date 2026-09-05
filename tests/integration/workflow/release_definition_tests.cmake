# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(variable IN ITEMS CMAKE_COMMAND PYTHON SOURCE_DIR TEST_ROOT)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()

set(root "${TEST_ROOT}/release-definitions")
file(REMOVE_RECURSE "${root}")
foreach(target IN ITEMS nix ebuild pkgbuild)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env SOURCE_DATE_EPOCH=1700000000
            "${PYTHON}" "${SOURCE_DIR}/tools/release.py"
            --target "${target}" --dist-dir "${root}/${target}" --skip-tests
        RESULT_VARIABLE result
        ERROR_VARIABLE error
        OUTPUT_QUIET
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${target} release definition failed: ${error}")
    endif()
    file(GLOB archives "${root}/${target}/*-${target}*.tar.gz")
    list(LENGTH archives archive_count)
    if(NOT archive_count EQUAL 1)
        message(FATAL_ERROR "Expected one ${target} definition archive")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar tf "${archives}"
        OUTPUT_VARIABLE entries
        RESULT_VARIABLE list_result
    )
    if(NOT list_result EQUAL 0 OR entries MATCHES "[.]in$")
        message(FATAL_ERROR "Invalid ${target} definition archive")
    endif()
endforeach()

file(GLOB pkgbuild_archives "${root}/pkgbuild/*-pkgbuild*.tar.gz")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${pkgbuild_archives}"
    OUTPUT_VARIABLE pkgbuild_entries
    RESULT_VARIABLE pkgbuild_list_result
)
if(NOT pkgbuild_list_result EQUAL 0 OR
   NOT pkgbuild_entries MATCHES "btrfs-backup[.]install")
    message(FATAL_ERROR "Arch definition is missing the pre-upgrade gate")
endif()

foreach(gate IN ITEMS
        "packaging/arch/btrfs-backup.install"
        "packaging/debian/preinst"
        "packaging/gentoo/btrfs-backup.ebuild.in"
        "packaging/rpm/btrfs-backup.spec.in")
    file(READ "${SOURCE_DIR}/${gate}" gate_content)
    if(NOT gate_content MATCHES "btrfs-backupctl upgrade preflight" OR
       NOT gate_content MATCHES "profile export-v4 --all")
        message(FATAL_ERROR "${gate} is missing the migration preflight contract")
    endif()
endforeach()
