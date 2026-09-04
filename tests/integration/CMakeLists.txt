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

find_program(BASH_EXECUTABLE bash REQUIRED)
find_program(PYTHON_EXECUTABLE python3 REQUIRED)

add_test(
    NAME bash-syntax-tests
    COMMAND
        ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DBASH=${BASH_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/workflow/bash_syntax_tests.cmake
)

add_test(
    NAME installation-render-tests
    COMMAND
        ${CMAKE_COMMAND}
        -DBACKUPCTL=$<TARGET_FILE:btrfs-backupctl>
        -DTEST_ROOT=${PROJECT_BINARY_DIR}/workflow-contracts
        -P ${CMAKE_CURRENT_SOURCE_DIR}/workflow/installation_render_tests.cmake
)

add_test(
    NAME real-btrfs-runner-cli-tests
    COMMAND
        ${PYTHON_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/docker/run_real_btrfs.py
        --help
)

add_test(
    NAME legacy-shell-harness-tests
    COMMAND
        ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/workflow/legacy_shell_harness_tests.cmake
)

add_test(
    NAME profile-json-cli-tests
    COMMAND
        ${CMAKE_COMMAND}
        -DBACKUPCTL=$<TARGET_FILE:btrfs-backupctl>
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DTEST_ROOT=${PROJECT_BINARY_DIR}/workflow-contracts
        -P ${CMAKE_CURRENT_SOURCE_DIR}/workflow/profile_json_cli_tests.cmake
)

add_test(
    NAME command-surface-tests
    COMMAND
        ${CMAKE_COMMAND}
        -DBACKUPCTL=$<TARGET_FILE:btrfs-backupctl>
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DTEST_ROOT=${PROJECT_BINARY_DIR}/workflow-contracts
        -P ${CMAKE_CURRENT_SOURCE_DIR}/workflow/command_surface_tests.cmake
)

add_test(
    NAME release-notes-tests
    COMMAND
        ${CMAKE_COMMAND}
        -DBASH=${BASH_EXECUTABLE}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DTEST_ROOT=${PROJECT_BINARY_DIR}/workflow-contracts
        -P ${CMAKE_CURRENT_SOURCE_DIR}/workflow/release_notes_tests.cmake
)

set_tests_properties(
    bash-syntax-tests
    real-btrfs-runner-cli-tests
    legacy-shell-harness-tests
    installation-render-tests
    profile-json-cli-tests
    command-surface-tests
    release-notes-tests
    PROPERTIES LABELS workflow
)

find_program(SYSTEMD_ANALYZE_EXECUTABLE systemd-analyze REQUIRED)

add_test(
    NAME systemd-backup-security-tests
    COMMAND
        ${CMAKE_COMMAND}
        -DUNIT_FILE=${PROJECT_SOURCE_DIR}/data/systemd/btrfs-backup@.service.example
        -DPOLICY=backup
        -DSYSTEMD_ANALYZE=${SYSTEMD_ANALYZE_EXECUTABLE}
        -DTEST_ROOT=${PROJECT_BINARY_DIR}/workflow-contracts
        -P ${PROJECT_SOURCE_DIR}/tests/systemd/security_tests.cmake
)
add_test(
    NAME systemd-device-preparation-security-tests
    COMMAND
        ${CMAKE_COMMAND}
        -DUNIT_FILE=${PROJECT_SOURCE_DIR}/data/systemd/btrfs-backup-device-preparation@.service
        -DPOLICY=device-preparation
        -DSYSTEMD_ANALYZE=${SYSTEMD_ANALYZE_EXECUTABLE}
        -DTEST_ROOT=${PROJECT_BINARY_DIR}/workflow-contracts
        -P ${PROJECT_SOURCE_DIR}/tests/systemd/security_tests.cmake
)
set_tests_properties(
    systemd-backup-security-tests
    systemd-device-preparation-security-tests
    PROPERTIES LABELS systemd
)

add_executable(btrfsbackup-real-restore-tests RealRestoreEngineTests.cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-real-restore-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(
    btrfsbackup-real-btrfs-tests
    RealBtrfsBackupTests.cpp
    support/IntegrationTestProcess.cpp
    support/RealBtrfsBrowseScenario.cpp
    support/RealBtrfsRecoveryScenarios.cpp
    support/RealBtrfsSafetyScenarios.cpp
    support/RealBtrfsTestEnvironment.cpp
    support/RealPackageTest.cpp
    support/RealProvisioningAdoptionScenario.cpp
    support/RealProvisioningFreeSpaceScenario.cpp
    support/RealProvisioningPartitionScenario.cpp
    support/RealProvisioningTestEnvironment.cpp
    support/RealProvisioningWholeDeviceScenario.cpp
)
target_link_libraries(btrfsbackup-real-btrfs-tests PRIVATE nlohmann_json::nlohmann_json)

add_executable(
    btrfsbackup-real-installed-runtime-tests
    RealInstalledRuntimeTests.cpp
    support/IntegrationTestProcess.cpp
    support/RealInstalledRuntimeTest.cpp
)
add_executable(
    btrfsbackup-real-mapper-lifecycle-tests
    RealMapperLifecycleTests.cpp
    support/IntegrationTestProcess.cpp
)
add_executable(
    btrfsbackup-real-trusted-hook-tests
    RealTrustedHookTests.cpp
    support/IntegrationTestProcess.cpp
    support/RealTrustedHookTest.cpp
)
target_link_libraries(btrfsbackup-real-trusted-hook-tests PRIVATE nlohmann_json::nlohmann_json)
add_executable(
    btrfsbackup-real-sandboxed-systemd-tests
    RealSandboxedSystemdTests.cpp
    support/IntegrationTestProcess.cpp
    support/RealSandboxedSystemdTest.cpp
)
target_link_libraries(btrfsbackup-real-sandboxed-systemd-tests PRIVATE nlohmann_json::nlohmann_json)
add_executable(
    btrfsbackup-real-manager-independence-tests
    RealManagerIndependenceTests.cpp
    support/IntegrationTestProcess.cpp
    support/RealManagerIndependenceTest.cpp
)
target_link_libraries(btrfsbackup-real-manager-independence-tests PRIVATE nlohmann_json::nlohmann_json)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-real-btrfs-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-real-installed-runtime-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-real-mapper-lifecycle-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-real-trusted-hook-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-real-sandboxed-systemd-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-real-manager-independence-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BTRFSBACKUP_REAL_RESTORE_TESTS)
    add_test(
        NAME real-restore-engine-tests
        COMMAND
            $<TARGET_FILE:btrfsbackup-real-restore-tests>
            $<TARGET_FILE:btrfs-backupctl>
    )
    set_tests_properties(
        real-restore-engine-tests
        PROPERTIES LABELS "real-btrfs;restore" TIMEOUT 120
    )
endif()

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
        btrfsbackup-real-system-dbus-backup-tests
        RealSystemDbusBackupTests.cpp
        support/IntegrationTestProcess.cpp
        support/RealSystemDbusBackupTest.cpp
    )
    target_link_libraries(
        btrfsbackup-real-system-dbus-backup-tests
        PRIVATE
            btrfsbackup-integration-manager-client
            nlohmann_json::nlohmann_json
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
        target_compile_options(btrfsbackup-real-system-dbus-backup-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME integration-client-cli-tests
        COMMAND
            ${CMAKE_COMMAND}
            -DBROWSE_CLIENT=$<TARGET_FILE:btrfsbackup-integration-browse-session-client>
            -DPROVISIONING_CLIENT=$<TARGET_FILE:btrfsbackup-integration-device-provisioning-client>
            -DINSTALLED_RUNTIME_TEST=$<TARGET_FILE:btrfsbackup-real-installed-runtime-tests>
            -DMAPPER_LIFECYCLE_TEST=$<TARGET_FILE:btrfsbackup-real-mapper-lifecycle-tests>
            -DTRUSTED_HOOK_TEST=$<TARGET_FILE:btrfsbackup-real-trusted-hook-tests>
            -DSANDBOXED_SYSTEMD_TEST=$<TARGET_FILE:btrfsbackup-real-sandboxed-systemd-tests>
            -DSYSTEM_DBUS_BACKUP_TEST=$<TARGET_FILE:btrfsbackup-real-system-dbus-backup-tests>
            -DMANAGER_INDEPENDENCE_TEST=$<TARGET_FILE:btrfsbackup-real-manager-independence-tests>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/integration_client_cli_tests.cmake
    )

    add_custom_target(
        real-btrfs-integration
        COMMAND
            ${CMAKE_COMMAND} -E env
            BTRFSBACKUP_REAL_BTRFS_TESTS=$<TARGET_FILE:btrfsbackup-real-btrfs-tests>
            BTRFSBACKUP_REAL_INSTALLED_RUNTIME_TESTS=$<TARGET_FILE:btrfsbackup-real-installed-runtime-tests>
            BTRFSBACKUP_REAL_MAPPER_LIFECYCLE_TESTS=$<TARGET_FILE:btrfsbackup-real-mapper-lifecycle-tests>
            BTRFSBACKUP_REAL_TRUSTED_HOOK_TESTS=$<TARGET_FILE:btrfsbackup-real-trusted-hook-tests>
            BTRFSBACKUP_REAL_SANDBOXED_SYSTEMD_TESTS=$<TARGET_FILE:btrfsbackup-real-sandboxed-systemd-tests>
            BTRFSBACKUP_REAL_SYSTEM_DBUS_BACKUP_TESTS=$<TARGET_FILE:btrfsbackup-real-system-dbus-backup-tests>
            BTRFSBACKUP_REAL_MANAGER_INDEPENDENCE_TESTS=$<TARGET_FILE:btrfsbackup-real-manager-independence-tests>
            BTRFSBACKUP_BROWSE_SESSION_CLIENT=$<TARGET_FILE:btrfsbackup-integration-browse-session-client>
            BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT=$<TARGET_FILE:btrfsbackup-integration-device-provisioning-client>
            ${PROJECT_SOURCE_DIR}/tests/integration/docker/run_real_btrfs.py
        DEPENDS
            btrfsbackup-real-btrfs-tests
            btrfsbackup-real-installed-runtime-tests
            btrfsbackup-real-mapper-lifecycle-tests
            btrfsbackup-real-trusted-hook-tests
            btrfsbackup-real-sandboxed-systemd-tests
            btrfsbackup-real-system-dbus-backup-tests
            btrfsbackup-real-manager-independence-tests
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
        add_executable(btrfsbackup-dbus-manager-tests DbusManagerTests.cpp)
        target_link_libraries(
            btrfsbackup-dbus-manager-tests
            PRIVATE btrfsbackup-manager-protocol
        )
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(btrfsbackup-dbus-manager-tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
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
                $<TARGET_FILE:btrfsbackup-dbus-manager-tests>
                $<TARGET_FILE:btrfs-backupd>
                ${DBUS_DAEMON_EXECUTABLE}
                ${BUSCTL_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/data/dbus/io.github.btrfsbackup.Manager1.conf
                $<TARGET_FILE:btrfsbackup-fake-polkit-authority>
                ${DBUS_MANAGER_TEST_EXTRA_ARGS}
        )
        set_tests_properties(dbus-manager-tests PROPERTIES LABELS dbus TIMEOUT 20)
    endif()
endif()
