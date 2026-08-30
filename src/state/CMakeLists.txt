# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-state-persistence-ports INTERFACE)
target_compile_features(btrfsbackup-state-persistence-ports INTERFACE cxx_std_23)
target_restricted_include_directories(btrfsbackup-state-persistence-ports
    INTERFACE_HEADERS
        state/PersistentDocumentOperations.hpp
)

add_library(btrfsbackup-state-model STATIC
    RunStatus.cpp
)

target_compile_features(btrfsbackup-state-model PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-state-model
    PUBLIC_HEADERS
        state/RunStatus.hpp
)
target_link_libraries(btrfsbackup-state-model PUBLIC btrfsbackup-core)

add_library(btrfsbackup-state STATIC
    BackupRunSerialization.cpp
    FileActiveRunRegistration.cpp
    FileCancellationMonitor.cpp
    FilePendingMarkerStore.cpp
    FileRunStateRepository.cpp
    JsonFileBackupRunCheckpointStore.cpp
    PollingCancellationWatch.cpp
    RunHistory.cpp
    RunState.cpp
    RunStatusBuilder.cpp
    RunStatusProjection.cpp
    StatusService.cpp
    StatusWriter.cpp
)

target_compile_features(btrfsbackup-state PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-state
    PUBLIC_HEADERS
        state/FilePendingMarkerStore.hpp
        state/FileCancellationMonitor.hpp
        state/FileRunStateRepository.hpp
        state/JsonFileBackupRunCheckpointStore.hpp
        state/RunHistory.hpp
        state/RunState.hpp
        state/RunStatusProjection.hpp
        state/RunStatusBuilder.hpp
        state/BackupRunSerialization.hpp
        state/StatusService.hpp
        state/StatusWriter.hpp
    PRIVATE_HEADERS
        state/FileActiveRunRegistration.hpp
        state/PollingCancellationWatch.hpp
)
target_link_libraries(btrfsbackup-state
    PUBLIC
        btrfsbackup-config-json
        btrfsbackup-backup-ports
        btrfsbackup-state-model
        btrfsbackup-state-persistence-ports
    PRIVATE
        btrfsbackup-backup-model
        btrfsbackup-config-domain
        btrfsbackup-core
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-state-model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-state PRIVATE -Wall -Wextra -Wpedantic)
endif()
