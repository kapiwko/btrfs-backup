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
    model/RunStatus.cpp
)

target_compile_features(btrfsbackup-state-model PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-state-model
    PUBLIC_HEADERS
        state/model/RunStatus.hpp
)
target_link_libraries(btrfsbackup-state-model PUBLIC btrfsbackup-core)

add_library(btrfsbackup-status-documents STATIC
    document/BoundedDocumentReader.cpp
    document/RunStatusDocumentCodec.cpp
)
target_compile_features(btrfsbackup-status-documents PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-status-documents
    PUBLIC_HEADERS
        state/document/BoundedDocumentReader.hpp
        state/document/RunStatusDocumentCodec.hpp
)
target_link_libraries(btrfsbackup-status-documents
    PUBLIC
        btrfsbackup-state-model
    PRIVATE
        btrfsbackup-config-json
        btrfsbackup-core
)

add_library(btrfsbackup-state STATIC
    cancellation/FileActiveRunRegistration.cpp
    cancellation/FileCancellationMonitor.cpp
    cancellation/PollingCancellationWatch.cpp
    persistence/BackupRunSerialization.cpp
    persistence/FilePendingMarkerStore.cpp
    persistence/FileRunStateRepository.cpp
    persistence/JsonFileBackupRunCheckpointStore.cpp
    persistence/StatusWriter.cpp
    projection/RunStatusBuilder.cpp
    projection/RunStatusProjection.cpp
    query/RunHistory.cpp
    query/RunState.cpp
    query/StatusService.cpp
)

target_compile_features(btrfsbackup-state PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-state
    PUBLIC_HEADERS
        state/cancellation/FileCancellationMonitor.hpp
        state/persistence/BackupRunSerialization.hpp
        state/persistence/FilePendingMarkerStore.hpp
        state/persistence/FileRunStateRepository.hpp
        state/persistence/JsonFileBackupRunCheckpointStore.hpp
        state/persistence/StatusWriter.hpp
        state/projection/RunStatusBuilder.hpp
        state/projection/RunStatusProjection.hpp
        state/query/RunHistory.hpp
        state/query/RunState.hpp
        state/query/StatusService.hpp
    PRIVATE_HEADERS
        state/cancellation/FileActiveRunRegistration.hpp
        state/cancellation/PollingCancellationWatch.hpp
)
target_link_libraries(btrfsbackup-state
    PUBLIC
        btrfsbackup-backup-ports
        btrfsbackup-config-json
        btrfsbackup-state-persistence-ports
        btrfsbackup-status-documents
    PRIVATE
        btrfsbackup-backup-model
        btrfsbackup-config-domain
        btrfsbackup-core
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-state-model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-status-documents PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-state PRIVATE -Wall -Wextra -Wpedantic)
endif()
