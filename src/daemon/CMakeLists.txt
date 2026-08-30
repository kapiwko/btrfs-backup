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
        btrfsbackup-daemon-core
    PRIVATE
        btrfsbackup-config-domain
        btrfsbackup-config-json
        btrfsbackup-core
        btrfsbackup-platform-linux-base
        btrfsbackup-platform-linux-storage
)

add_library(btrfsbackup-daemon-control STATIC
    control/AuthorizedOperationCommand.cpp
    control/OperationalControlService.cpp
)
target_compile_features(btrfsbackup-daemon-control PUBLIC cxx_std_23)
target_compile_definitions(
    btrfsbackup-daemon-control
    PRIVATE BTRFSBACKUP_INSTALL_BINDIR="${CMAKE_INSTALL_FULL_BINDIR}"
)
target_restricted_include_directories(btrfsbackup-daemon-control
    PUBLIC_HEADERS
        daemon/control/AuthorizedOperationCommand.hpp
        daemon/control/OperationalControlService.hpp
        daemon/control/SystemdUnitController.hpp
)
target_link_libraries(btrfsbackup-daemon-control
    PUBLIC
        btrfsbackup-config-domain
        btrfsbackup-daemon-core
    PRIVATE
        btrfsbackup-core
        btrfsbackup-manager-protocol
)

add_library(btrfsbackup-daemon-system-control STATIC
    control/CommandSystemdUnitController.cpp
    control/OperationEnvironmentFile.cpp
    control/SystemOperationalControlBackend.cpp
)
target_compile_features(btrfsbackup-daemon-system-control PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-daemon-system-control
    PUBLIC_HEADERS
        daemon/control/CommandSystemdUnitController.hpp
        daemon/control/SystemOperationalControlBackend.hpp
    PRIVATE_HEADERS
        daemon/control/OperationEnvironmentFile.hpp
)
target_link_libraries(btrfsbackup-daemon-system-control
    PUBLIC
        btrfsbackup-backup-ports
        btrfsbackup-config-ports
        btrfsbackup-daemon-control
    PRIVATE
        btrfsbackup-platform-linux-filesystem
        btrfsbackup-platform-linux-process
)

add_library(btrfsbackup-daemon-dbus STATIC
    dbus/ManagerChangeMonitor.cpp
    dbus/ManagerDbusObject.cpp
    dbus/ManagerDbusServer.cpp
    dbus/ManagerErrorMapper.cpp
    dbus/ManagerJsonCodec.cpp
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
        daemon/dbus/ManagerDbusObject.hpp
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
set_target_properties(btrfs-backupd PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_link_libraries(btrfs-backupd
    PRIVATE
        btrfsbackup-daemon-dbus
        btrfsbackup-daemon-system-control
        btrfsbackup-platform-linux-config
        btrfsbackup-platform-linux-filesystem
        btrfsbackup-platform-linux-process
        btrfsbackup-state
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-daemon-control PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-daemon-core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-daemon-dbus PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-daemon-query PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-daemon-system-control PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfs-backupd PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS btrfs-backupd RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
