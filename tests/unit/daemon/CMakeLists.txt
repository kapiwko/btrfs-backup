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
