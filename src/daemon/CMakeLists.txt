# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-daemon-core STATIC
    AuthorizedOperationCommand.cpp
    DeviceStateQueryService.cpp
    HistoryQueryService.cpp
    ManagerDocumentReader.cpp
    ManagerAuditLog.cpp
    ManagerErrorMapper.cpp
    ManagerJsonCodec.cpp
    ManagerService.cpp
    OperationalControlService.cpp
    ProfileQueryService.cpp
    StatusQueryService.cpp
)
target_compile_features(btrfsbackup-daemon-core PUBLIC cxx_std_20)
target_compile_definitions(
    btrfsbackup-daemon-core
    PRIVATE BTRFSBACKUP_INSTALL_BINDIR="${CMAKE_INSTALL_FULL_BINDIR}"
)
target_restricted_include_directories(btrfsbackup-daemon-core
    PUBLIC_HEADERS
        daemon/AuthorizedOperationCommand.hpp
        daemon/DeviceStateQueryService.hpp
        daemon/HistoryQueryService.hpp
        daemon/ManagerErrorMapper.hpp
        daemon/ManagerErrors.hpp
        daemon/ManagerJsonCodec.hpp
        daemon/ManagerAuditLog.hpp
        daemon/ManagerPaths.hpp
        daemon/ManagerResponseModels.hpp
        daemon/ManagerService.hpp
        daemon/OperationalControlService.hpp
        daemon/ProfileQueryService.hpp
        daemon/StatusQueryService.hpp
        daemon/SystemdUnitController.hpp
    PRIVATE_HEADERS
        daemon/ManagerDocumentReader.hpp
)
target_link_libraries(btrfsbackup-daemon-core
    PUBLIC
        btrfsbackup-config-domain
    PRIVATE
        btrfsbackup-config-json
        btrfsbackup-core
        btrfsbackup-platform-linux
)

add_library(btrfsbackup-daemon-system-control STATIC
    CommandSystemdUnitController.cpp
    OperationEnvironmentFile.cpp
    SystemOperationalControlBackend.cpp
)
target_compile_features(btrfsbackup-daemon-system-control PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-daemon-system-control
    PUBLIC_HEADERS
        daemon/CommandSystemdUnitController.hpp
        daemon/SystemOperationalControlBackend.hpp
    PRIVATE_HEADERS
        daemon/OperationEnvironmentFile.hpp
)
target_link_libraries(btrfsbackup-daemon-system-control
    PUBLIC
        btrfsbackup-backup-ports
        btrfsbackup-config-ports
        btrfsbackup-daemon-core
    PRIVATE
        btrfsbackup-platform-linux
)

add_executable(btrfs-backupd
    ManagerDbusObject.cpp
    ManagerDbusServer.cpp
    main.cpp
    ManagerChangeMonitor.cpp
    PolkitAuthorizer.cpp
)
target_restricted_include_directories(btrfs-backupd
    PRIVATE_HEADERS
        daemon/ManagerDbusObject.hpp
        daemon/ManagerDbusServer.hpp
        daemon/ManagerChangeMonitor.hpp
        daemon/PolkitAuthorizer.hpp
)
set_target_properties(btrfs-backupd PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_link_libraries(btrfs-backupd
    PRIVATE
        btrfsbackup-backup-ports
        btrfsbackup-config-ports
        btrfsbackup-daemon-core
        btrfsbackup-daemon-system-control
        btrfsbackup-platform-linux
        btrfsbackup-platform-linux-config
        btrfsbackup-state
        PkgConfig::SYSTEMD
        PkgConfig::UDEV
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-daemon-core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-daemon-system-control PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfs-backupd PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS btrfs-backupd RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
