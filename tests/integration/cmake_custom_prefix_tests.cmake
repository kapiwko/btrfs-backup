# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_ROOT OR NOT DEFINED INSTALL_LIBDIR)
    message(FATAL_ERROR "SOURCE_DIR, BINARY_ROOT, and INSTALL_LIBDIR are required")
endif()

set(custom_prefix "/opt/btrfs-backup")
file(REMOVE_RECURSE "${BINARY_ROOT}")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        -S "${SOURCE_DIR}"
        -B "${BINARY_ROOT}"
        -DCMAKE_INSTALL_PREFIX=${custom_prefix}
        -DCMAKE_INSTALL_LIBDIR=${INSTALL_LIBDIR}
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        -DBUILD_TESTING=OFF
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "custom-prefix configure failed:\n${configure_output}\n${configure_error}")
endif()

set(expected_bindir "${custom_prefix}/bin")
set(generated_files
    "btrfs-backup@.service"
    "btrfs-backup-eject@.service"
    "btrfs-backup-validate@.service"
    "btrfs-backupd.service"
    "io.github.btrfsbackup.Manager1.service"
)
foreach(generated_file IN LISTS generated_files)
    file(READ "${BINARY_ROOT}/${generated_file}" content)
    if(NOT content MATCHES "${expected_bindir}/btrfs-backup")
        message(FATAL_ERROR "${generated_file} does not use custom install prefix")
    endif()
    if(content MATCHES "/usr/bin/btrfs-backup")
        message(FATAL_ERROR "${generated_file} retained a hard-coded application path")
    endif()
endforeach()

file(READ "${BINARY_ROOT}/compile_commands.json" compile_commands)
if(NOT compile_commands MATCHES "${expected_bindir}")
    message(FATAL_ERROR "custom bindir was not propagated to compiled transient-unit builders")
endif()
