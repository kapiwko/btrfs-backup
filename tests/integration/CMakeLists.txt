# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(
    btrfsbackup-profile-schema-contract-tests
    ProfileSchemaContractTests.cpp
    profile-schema-contract-tests
    btrfsbackup-config-json
    btrfsbackup-manager-protocol
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
        -DINSTALL_BINDIR_FULL=${CMAKE_INSTALL_FULL_BINDIR}
        -DBUILD_SYSTEM_MANAGER=${BUILD_SYSTEM_MANAGER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake_install_tests.cmake
)

add_test(
    NAME cmake-custom-prefix-tests
    COMMAND
        ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DBINARY_ROOT=${PROJECT_BINARY_DIR}/cmake-custom-prefix-test
        -DINSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake_custom_prefix_tests.cmake
)

if(BUILD_SYSTEM_MANAGER)
    find_program(DBUS_DAEMON_EXECUTABLE dbus-daemon)
    find_program(BUSCTL_EXECUTABLE busctl)
    if(DBUS_DAEMON_EXECUTABLE AND BUSCTL_EXECUTABLE)
        add_executable(btrfsbackup-fake-polkit-authority FakePolkitAuthority.cpp)
        target_link_libraries(btrfsbackup-fake-polkit-authority PRIVATE PkgConfig::SYSTEMD)
        set(DBUS_MANAGER_TEST_EXTRA_ARGS)
        if(TARGET btrfsbackup_kde_models)
            find_program(
                DBUS_TEST_QML_EXECUTABLE
                NAMES qml
                PATHS /usr/lib/qt6/bin
                NO_DEFAULT_PATH
            )
            find_program(DBUS_TEST_QML_EXECUTABLE NAMES qml)
            if(DBUS_TEST_QML_EXECUTABLE)
                list(APPEND DBUS_MANAGER_TEST_EXTRA_ARGS
                    ${DBUS_TEST_QML_EXECUTABLE}
                    ${PROJECT_BINARY_DIR}
                    ${PROJECT_SOURCE_DIR}/integrations/kde/plasmoid/tests/backend-dbus.qml
                )
            endif()
        endif()
        add_test(
            NAME dbus-manager-tests
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/dbus_manager_test.sh
                $<TARGET_FILE:btrfs-backupd>
                ${DBUS_DAEMON_EXECUTABLE}
                ${BUSCTL_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/data/dbus/io.github.btrfsbackup.Manager1.conf
                $<TARGET_FILE:btrfsbackup-fake-polkit-authority>
                ${DBUS_MANAGER_TEST_EXTRA_ARGS}
        )
        set_tests_properties(dbus-manager-tests PROPERTIES TIMEOUT 20)
    endif()
endif()
