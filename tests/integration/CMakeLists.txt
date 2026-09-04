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
    add_library(
        btrfsbackup-integration-manager-client STATIC
        support/ManagerTestClient.cpp
    )
    target_link_libraries(
        btrfsbackup-integration-manager-client
        PUBLIC btrfsbackup-manager-protocol
        PRIVATE PkgConfig::SYSTEMD
    )
    target_include_directories(
        btrfsbackup-integration-manager-client
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support
    )

    add_executable(
        btrfsbackup-integration-browse-session-client
        support/BrowseSessionClient.cpp
    )
    target_link_libraries(
        btrfsbackup-integration-browse-session-client
        PRIVATE
            btrfsbackup-integration-manager-client
            nlohmann_json::nlohmann_json
    )

    add_executable(
        btrfsbackup-integration-device-provisioning-client
        support/DeviceProvisioningClient.cpp
    )
    target_link_libraries(
        btrfsbackup-integration-device-provisioning-client
        PRIVATE
            btrfsbackup-integration-manager-client
            btrfsbackup-platform-linux-base
            nlohmann_json::nlohmann_json
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(btrfsbackup-integration-manager-client PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(btrfsbackup-integration-browse-session-client PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(btrfsbackup-integration-device-provisioning-client PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME integration-client-cli-tests
        COMMAND
            ${CMAKE_COMMAND}
            -DBROWSE_CLIENT=$<TARGET_FILE:btrfsbackup-integration-browse-session-client>
            -DPROVISIONING_CLIENT=$<TARGET_FILE:btrfsbackup-integration-device-provisioning-client>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/integration_client_cli_tests.cmake
    )

    add_custom_target(
        real-btrfs-integration
        COMMAND
            ${CMAKE_COMMAND} -E env
            BTRFSBACKUP_BROWSE_SESSION_CLIENT=$<TARGET_FILE:btrfsbackup-integration-browse-session-client>
            BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT=$<TARGET_FILE:btrfsbackup-integration-device-provisioning-client>
            ${PROJECT_SOURCE_DIR}/tests/integration/docker/run-real-btrfs.sh
        DEPENDS
            btrfsbackup-integration-browse-session-client
            btrfsbackup-integration-device-provisioning-client
        USES_TERMINAL
    )

    add_custom_target(
        qemu-hotplug-integration
        COMMAND
            ${CMAKE_COMMAND} -E env
            BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT=$<TARGET_FILE:btrfsbackup-integration-device-provisioning-client>
            ${PROJECT_SOURCE_DIR}/tests/qemu/run-hotplug.sh
        DEPENDS btrfsbackup-integration-device-provisioning-client
        USES_TERMINAL
    )

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
