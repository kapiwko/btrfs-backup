# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(
    btrfsbackup-profile-schema-contract-tests
    profile_schema_contract_tests.cpp
    profile-schema-contract-tests
    btrfsbackup-config-model
)

target_compile_definitions(btrfsbackup-profile-schema-contract-tests
    PRIVATE BTRFSBACKUP_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

add_test(
    NAME cmake-install-tests
    COMMAND
        ${CMAKE_COMMAND}
        -DBUILD_DIR=${PROJECT_BINARY_DIR}
        -DINSTALL_ROOT=${PROJECT_BINARY_DIR}/cmake-install-test
        -DINSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
        -DBUILD_SYSTEM_MANAGER=${BUILD_SYSTEM_MANAGER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake_install_tests.cmake
)

if(BUILD_SYSTEM_MANAGER)
    find_program(DBUS_DAEMON_EXECUTABLE dbus-daemon)
    find_program(BUSCTL_EXECUTABLE busctl)
    if(DBUS_DAEMON_EXECUTABLE AND BUSCTL_EXECUTABLE)
        add_test(
            NAME dbus-manager-tests
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/dbus_manager_test.sh
                $<TARGET_FILE:btrfs-backupd>
                ${DBUS_DAEMON_EXECUTABLE}
                ${BUSCTL_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/data/dbus/io.github.btrfsbackup.Manager1.conf
        )
        set_tests_properties(dbus-manager-tests PROPERTIES TIMEOUT 20)
    endif()
endif()
