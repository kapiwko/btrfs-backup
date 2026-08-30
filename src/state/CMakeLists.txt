# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-state-persistence-ports INTERFACE)
target_compile_features(btrfsbackup-state-persistence-ports INTERFACE cxx_std_23)
target_restricted_include_directories(btrfsbackup-state-persistence-ports
    INTERFACE_HEADERS
        state/persistence/PersistentDocumentOperations.hpp
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
    persistence/BackupRunSerialization.cpp
    cancellation/FileActiveRunRegistration.cpp
    cancellation/FileCancellationMonitor.cpp
    persistence/FilePendingMarkerStore.cpp
    persistence/FileRunStateRepository.cpp
    persistence/JsonFileBackupRunCheckpointStore.cpp
    cancellation/PollingCancellationWatch.cpp
    RunHistory.cpp
    RunState.cpp
    RunStatusBuilder.cpp
    RunStatusProjection.cpp
    StatusService.cpp
    persistence/StatusWriter.cpp
)

target_compile_features(btrfsbackup-state PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-state
    PUBLIC_HEADERS
        state/persistence/FilePendingMarkerStore.hpp
        state/cancellation/FileCancellationMonitor.hpp
        state/persistence/FileRunStateRepository.hpp
        state/persistence/JsonFileBackupRunCheckpointStore.hpp
        state/RunHistory.hpp
        state/RunState.hpp
        state/RunStatusProjection.hpp
        state/RunStatusBuilder.hpp
        state/persistence/BackupRunSerialization.hpp
        state/StatusService.hpp
        state/persistence/StatusWriter.hpp
    PRIVATE_HEADERS
        state/cancellation/FileActiveRunRegistration.hpp
        state/cancellation/PollingCancellationWatch.hpp
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
