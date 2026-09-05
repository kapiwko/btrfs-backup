# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(
    btrfsbackup-authorized-operation-command-tests
    AuthorizedOperationCommandTests.cpp
    authorized-operation-command-tests
    btrfsbackup-daemon-control
)
target_compile_definitions(
    btrfsbackup-authorized-operation-command-tests
    PRIVATE BTRFSBACKUP_TEST_INSTALL_BINDIR="${CMAKE_INSTALL_FULL_BINDIR}"
)

add_btrfsbackup_test(
    btrfsbackup-manager-audit-log-tests
    ManagerAuditLogTests.cpp
    manager-audit-log-tests
    btrfsbackup-daemon-core
    btrfsbackup-config-json
)

add_btrfsbackup_test(
    btrfsbackup-manager-service-tests
    ManagerServiceTests.cpp
    manager-service-tests
    btrfsbackup-daemon-query
    btrfsbackup-config-json
)

add_btrfsbackup_test(
    btrfsbackup-device-state-query-service-tests
    DeviceStateQueryServiceTests.cpp
    device-state-query-service-tests
    btrfsbackup-daemon-query
    btrfsbackup-config-json
    btrfsbackup-state
)

add_btrfsbackup_test(
    btrfsbackup-manager-json-codec-tests
    ManagerJsonCodecTests.cpp
    manager-json-codec-tests
    btrfsbackup-daemon-dbus
    btrfsbackup-config-json
)

add_btrfsbackup_test(
    btrfsbackup-manager-error-mapper-tests
    ManagerErrorMapperTests.cpp
    manager-error-mapper-tests
    btrfsbackup-daemon-dbus
    btrfsbackup-core
)

add_btrfsbackup_test(
    btrfsbackup-operational-control-service-tests
    OperationalControlServiceTests.cpp
    operational-control-service-tests
    btrfsbackup-daemon-control
    btrfsbackup-core
)

add_btrfsbackup_test(
    btrfsbackup-browse-session-service-tests
    BrowseSessionServiceTests.cpp
    browse-session-service-tests
    btrfsbackup-daemon-control
)

add_btrfsbackup_test(
    btrfsbackup-browse-directory-page-collector-tests
    BrowseDirectoryPageCollectorTests.cpp
    browse-directory-page-collector-tests
    btrfsbackup-daemon-system-control
)

add_btrfsbackup_test(
    btrfsbackup-profile-administration-service-tests
    ProfileAdministrationServiceTests.cpp
    profile-administration-service-tests
    btrfsbackup-daemon-control
    btrfsbackup-config-json
)

add_btrfsbackup_test(
    btrfsbackup-credential-administration-service-tests
    CredentialAdministrationServiceTests.cpp
    credential-administration-service-tests
    btrfsbackup-daemon-control
)

add_btrfsbackup_test(
    btrfsbackup-device-provisioning-service-tests
    DeviceProvisioningServiceTests.cpp
    device-provisioning-service-tests
    btrfsbackup-daemon-control
)

add_btrfsbackup_test(
    btrfsbackup-existing-target-inspector-tests
    ExistingTargetInspectorTests.cpp
    existing-target-inspector-tests
    btrfsbackup-daemon-system-control
    btrfsbackup-platform-linux-provisioning
    btrfsbackup-platform-linux-storage
)

add_btrfsbackup_test(
    btrfsbackup-system-device-provisioning-backend-tests
    SystemDeviceProvisioningBackendTests.cpp
    system-device-provisioning-backend-tests
    btrfsbackup-daemon-system-control
    btrfsbackup-provisioning
    btrfsbackup-platform-linux-config
    btrfsbackup-platform-linux-provisioning
    btrfsbackup-platform-linux-storage
)

add_btrfsbackup_test(
    btrfsbackup-system-credential-administration-backend-tests
    SystemCredentialAdministrationBackendTests.cpp
    system-credential-administration-backend-tests
    btrfsbackup-daemon-system-control
    btrfsbackup-config-json
    btrfsbackup-platform-linux-config
    btrfsbackup-platform-linux-storage
)

add_btrfsbackup_test(
    btrfsbackup-device-preparation-transaction-tests
    DevicePreparationTransactionTests.cpp
    device-preparation-transaction-tests
    btrfsbackup-daemon-system-control
)

add_btrfsbackup_test(
    btrfsbackup-destructive-device-safety-inspector-tests
    DestructiveDeviceSafetyInspectorTests.cpp
    destructive-device-safety-inspector-tests
    btrfsbackup-daemon-system-control
    btrfsbackup-provisioning
)

add_btrfsbackup_test(
    btrfsbackup-system-profile-administration-backend-tests
    SystemProfileAdministrationBackendTests.cpp
    system-profile-administration-backend-tests
    btrfsbackup-daemon-system-control
    btrfsbackup-config-json
    btrfsbackup-platform-linux-config
)

add_btrfsbackup_test(
    btrfsbackup-systemd-control-tests
    SystemdControlTests.cpp
    systemd-control-tests
    btrfsbackup-daemon-system-control
)

add_btrfsbackup_test(
    btrfsbackup-dbus-callback-boundary-tests
    DbusCallbackBoundaryTests.cpp
    dbus-callback-boundary-tests
    btrfsbackup-daemon-dbus
)
