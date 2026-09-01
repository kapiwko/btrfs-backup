# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-daemon-core STATIC
    ManagerAuditLog.cpp
)
target_compile_features(btrfsbackup-daemon-core PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-daemon-core
    PUBLIC_HEADERS
        daemon/ManagerAuditLog.hpp
        daemon/ManagerPaths.hpp
        daemon/ManagerResponseModels.hpp
        daemon/dbus/ManagerErrors.hpp
)
target_link_libraries(btrfsbackup-daemon-core
    PUBLIC
        btrfsbackup-manager-protocol
        btrfsbackup-status-documents
    PRIVATE
        btrfsbackup-config-json
        btrfsbackup-core
)

add_library(btrfsbackup-daemon-query STATIC
    ManagerService.cpp
    query/DeviceStateQueryService.cpp
    query/HistoryQueryService.cpp
    query/ManagerDocumentReader.cpp
    query/ProfileQueryService.cpp
    query/StatusQueryService.cpp
)
target_compile_features(btrfsbackup-daemon-query PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-daemon-query
    PUBLIC_HEADERS
        daemon/ManagerService.hpp
        daemon/query/DeviceStateQueryService.hpp
        daemon/query/HistoryQueryService.hpp
        daemon/query/ProfileQueryService.hpp
        daemon/query/StatusQueryService.hpp
    PRIVATE_HEADERS
        daemon/query/ManagerDocumentReader.hpp
)
target_link_libraries(btrfsbackup-daemon-query
    PUBLIC
        btrfsbackup-backup-ports
        btrfsbackup-daemon-core
    PRIVATE
        btrfsbackup-config-domain
        btrfsbackup-config-json
        btrfsbackup-core
        btrfsbackup-platform-linux-storage
        btrfsbackup-state
)

add_library(btrfsbackup-daemon-control STATIC
    control/AuthorizedOperationCommand.cpp
    control/BrowseSessionService.cpp
    control/CredentialAdministrationService.cpp
    control/DeviceProvisioningService.cpp
    control/OperationalControlService.cpp
    control/ProfileAdministrationService.cpp
)
target_compile_features(btrfsbackup-daemon-control PUBLIC cxx_std_23)
target_compile_definitions(
    btrfsbackup-daemon-control
    PRIVATE BTRFSBACKUP_INSTALL_BINDIR="${CMAKE_INSTALL_FULL_BINDIR}"
)
target_restricted_include_directories(btrfsbackup-daemon-control
    PUBLIC_HEADERS
        daemon/control/AuthorizedOperationCommand.hpp
        daemon/control/BrowseSessionService.hpp
        daemon/control/CredentialAdministrationService.hpp
        daemon/control/DeviceProvisioningService.hpp
        daemon/control/OperationalControlService.hpp
        daemon/control/ProfileAdministrationService.hpp
        daemon/control/SystemdUnitController.hpp
)
target_link_libraries(btrfsbackup-daemon-control
    PUBLIC
        btrfsbackup-config-domain
        btrfsbackup-daemon-core
    PRIVATE
        btrfsbackup-config-json
        btrfsbackup-core
        btrfsbackup-manager-protocol
)

add_library(btrfsbackup-daemon-system-control STATIC
    control/CommandSystemdUnitController.cpp
    control/DestructiveDeviceSafetyInspector.cpp
    control/DevicePreparationExecutor.cpp
    control/DevicePreparationPlanBuilder.cpp
    control/DevicePreparationTransaction.cpp
    control/DevicePreparationUnitController.cpp
    control/OperationEnvironmentFile.cpp
    control/ProvisioningDeviceEnumerator.cpp
    control/SystemOperationalControlBackend.cpp
    control/SystemBrowseSessionBackend.cpp
    control/SystemCredentialAdministrationBackend.cpp
    control/SystemDeviceProvisioningBackend.cpp
    control/SystemProfileAdministrationBackend.cpp
)
target_compile_features(btrfsbackup-daemon-system-control PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-daemon-system-control
    PUBLIC_HEADERS
        daemon/control/CommandSystemdUnitController.hpp
        daemon/control/DestructiveDeviceSafetyInspector.hpp
        daemon/control/DevicePreparationTransaction.hpp
        daemon/control/DevicePreparationUnitController.hpp
        daemon/control/SystemOperationalControlBackend.hpp
        daemon/control/SystemBrowseSessionBackend.hpp
        daemon/control/SystemCredentialAdministrationBackend.hpp
        daemon/control/SystemDeviceProvisioningBackend.hpp
        daemon/control/SystemProfileAdministrationBackend.hpp
    PRIVATE_HEADERS
        daemon/control/DevicePreparationExecutor.hpp
        daemon/control/DevicePreparationPlanBuilder.hpp
        daemon/control/OperationEnvironmentFile.hpp
        daemon/control/ProvisioningDeviceEnumerator.hpp
)
target_link_libraries(btrfsbackup-daemon-system-control
    PUBLIC
        btrfsbackup-config-ports
        btrfsbackup-daemon-control
        btrfsbackup-platform-linux-filesystem
    PRIVATE
        btrfsbackup-config-json
        btrfsbackup-config-wizard
        btrfsbackup-backup-planning
        btrfsbackup-platform-linux-config
        btrfsbackup-platform-linux-process
        btrfsbackup-platform-linux-storage
        btrfsbackup-platform-linux-transfer
)

add_library(btrfsbackup-daemon-dbus STATIC
    dbus/ManagerBrowseMethods.cpp
    dbus/ManagerChangeMonitor.cpp
    dbus/ManagerCredentialMethods.cpp
    dbus/ManagerDbusObject.cpp
    dbus/ManagerDbusServer.cpp
    dbus/ManagerErrorMapper.cpp
    dbus/ManagerJsonCodec.cpp
    dbus/ManagerMethodSupport.cpp
    dbus/ManagerOperationalMethods.cpp
    dbus/ManagerProfileMethods.cpp
    dbus/ManagerProvisioningMethods.cpp
    dbus/ManagerReadMethods.cpp
    dbus/PolkitAuthorizer.cpp
)
target_compile_features(btrfsbackup-daemon-dbus PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-daemon-dbus
    PUBLIC_HEADERS
        daemon/dbus/DbusCallbackBoundary.hpp
        daemon/dbus/ManagerDbusServer.hpp
        daemon/dbus/ManagerErrorMapper.hpp
        daemon/dbus/ManagerJsonCodec.hpp
    PRIVATE_HEADERS
        daemon/dbus/ManagerChangeMonitor.hpp
        daemon/dbus/ManagerBrowseMethods.hpp
        daemon/dbus/ManagerCredentialMethods.hpp
        daemon/dbus/ManagerDbusObject.hpp
        daemon/dbus/ManagerMethodSupport.hpp
        daemon/dbus/ManagerOperationalMethods.hpp
        daemon/dbus/ManagerProfileMethods.hpp
        daemon/dbus/ManagerProvisioningMethods.hpp
        daemon/dbus/ManagerReadMethods.hpp
        daemon/dbus/PolkitAuthorizer.hpp
)
target_link_libraries(btrfsbackup-daemon-dbus
    PUBLIC
        btrfsbackup-daemon-control
        btrfsbackup-daemon-query
    PRIVATE
        btrfsbackup-config-json
        btrfsbackup-core
        btrfsbackup-platform-linux-base
        PkgConfig::SYSTEMD
        PkgConfig::UDEV
)

add_executable(btrfs-backupd
    main.cpp
)

add_executable(btrfs-backup-device-preparation
    DevicePreparationMain.cpp
)
set_target_properties(
    btrfs-backup-device-preparation
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
)
target_link_libraries(btrfs-backup-device-preparation
    PRIVATE
        btrfsbackup-daemon-system-control
        btrfsbackup-platform-linux-config
        btrfsbackup-platform-linux-process
        btrfsbackup-platform-linux-storage
        btrfsbackup-platform-linux-systemd
)
set_target_properties(btrfs-backupd PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_link_libraries(btrfs-backupd
    PRIVATE
        btrfsbackup-daemon-dbus
        btrfsbackup-daemon-system-control
        btrfsbackup-platform-linux-config
        btrfsbackup-platform-linux-filesystem
        btrfsbackup-platform-linux-process
        btrfsbackup-platform-linux-storage
        btrfsbackup-platform-linux-systemd
        btrfsbackup-state
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-daemon-control PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-daemon-core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-daemon-dbus PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-daemon-query PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-daemon-system-control PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfs-backupd PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfs-backup-device-preparation PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(
    TARGETS btrfs-backupd btrfs-backup-device-preparation
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
