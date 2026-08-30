# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-backup-model STATIC
    model/BackupPlanningSnapshot.cpp
    model/BackupRunActions.cpp
    model/BackupRunPlan.cpp
    model/IncrementalParent.cpp
    model/PendingRecovery.cpp
    model/RetentionPlan.cpp
    model/SnapshotInventory.cpp
    model/TargetStorageMeasurement.cpp
)

target_compile_features(btrfsbackup-backup-model PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-backup-model
    PUBLIC_HEADERS
        backup/model/BackupExecution.hpp
        backup/model/BackupPlanningSnapshot.hpp
        backup/model/BackupRunAction.hpp
        backup/model/BackupRunActions.hpp
        backup/model/BackupRunCheckpoint.hpp
        backup/model/BackupRunEvent.hpp
        backup/model/BackupRunExecution.hpp
        backup/model/BackupRunPlan.hpp
        backup/model/IncrementalParent.hpp
        backup/model/PendingRecovery.hpp
        backup/model/RetentionPlan.hpp
        backup/model/Snapshot.hpp
        backup/model/SnapshotInventory.hpp
        backup/model/TargetStorageMeasurement.hpp
)
target_link_libraries(btrfsbackup-backup-model
    PUBLIC
        btrfsbackup-config-domain
    PRIVATE
        btrfsbackup-core
)

add_library(btrfsbackup-transfer STATIC
    transfer/AsyncTransfer.cpp
    transfer/ThreadedAsyncTransferHandle.cpp
    transfer/TransferEvent.cpp
    transfer/TransferResult.cpp
    transfer/TransferSpeedEstimator.cpp
)

target_compile_features(btrfsbackup-transfer PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-transfer
    PUBLIC_HEADERS
        backup/transfer/AsyncTransfer.hpp
        backup/transfer/ITransferPipeline.hpp
        backup/transfer/TransferEvent.hpp
        backup/transfer/TransferPlan.hpp
        backup/transfer/TransferResult.hpp
        backup/transfer/TransferSpeedEstimator.hpp
    PRIVATE_HEADERS
        backup/transfer/ThreadedAsyncTransferHandle.hpp
)
target_link_libraries(btrfsbackup-transfer
    PUBLIC
        btrfsbackup-core
)

add_library(btrfsbackup-backup-ports STATIC
    ports/IBtrfsOperations.cpp
    ports/ICommandRunner.cpp
    ports/IMountInspector.cpp
)

target_compile_features(btrfsbackup-backup-ports PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-backup-ports
    PUBLIC_HEADERS
        backup/ports/BackupRunActionHandlerFactory.hpp
        backup/ports/CancellationMonitor.hpp
        backup/ports/CancellationRequestStore.hpp
        backup/ports/CleanupDiagnostic.hpp
        backup/ports/IBackupDiscovery.hpp
        backup/ports/IBackupPlanBuilder.hpp
        backup/ports/IBackupPreflight.hpp
        backup/ports/IBackupRunCheckpointStore.hpp
        backup/ports/IBackupRunEventSink.hpp
        backup/ports/IBackupRunFactory.hpp
        backup/ports/IBtrfsOperations.hpp
        backup/ports/ICheckpointStoreFactory.hpp
        backup/ports/ICommandRunner.hpp
        backup/ports/IFileSystem.hpp
        backup/ports/IMountInspector.hpp
        backup/ports/IFilesystemSpaceProbe.hpp
        backup/ports/IPendingMarkerStore.hpp
        backup/ports/IRunEventSinkFactory.hpp
        backup/ports/IRunLedger.hpp
        backup/ports/ITargetStorageMeasurementStore.hpp
        backup/ports/Process.hpp
        backup/ports/RunContext.hpp
        backup/ports/RunLease.hpp
        backup/ports/SafeDirectory.hpp
        backup/ports/TargetManager.hpp
        backup/ports/TrustedExecutable.hpp
)
target_link_libraries(btrfsbackup-backup-ports
    PUBLIC
        btrfsbackup-backup-model
        btrfsbackup-transfer
    PRIVATE
        btrfsbackup-config-domain
        btrfsbackup-core
)

add_library(btrfsbackup-backup-testing STATIC
    testing/NullBackupRunEventSink.cpp
)

target_compile_features(btrfsbackup-backup-testing PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-backup-testing
    PUBLIC_HEADERS
        backup/testing/NullBackupRunEventSink.hpp
)
target_link_libraries(btrfsbackup-backup-testing
    PUBLIC
        btrfsbackup-backup-ports
)

add_library(btrfsbackup-backup-planning STATIC
    planning/BackupDiscovery.cpp
    planning/BackupPlanBuilder.cpp
    planning/BackupPreflight.cpp
    planning/BackupPreflightValidation.cpp
)
target_compile_features(btrfsbackup-backup-planning PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-backup-planning
    PUBLIC_HEADERS
        backup/planning/BackupDiscovery.hpp
        backup/planning/BackupPlanBuilder.hpp
        backup/planning/BackupPreflight.hpp
        backup/planning/BackupPreflightValidation.hpp
)
target_link_libraries(btrfsbackup-backup-planning
    PUBLIC
        btrfsbackup-backup-ports
    PRIVATE
        btrfsbackup-backup-model
        btrfsbackup-config-domain
        btrfsbackup-core
)

add_library(btrfsbackup-backup-execution STATIC
    execution/BackupActionExecutor.cpp
    execution/BackupRun.cpp
    execution/BackupRunCheckpointPolicy.cpp
    execution/BackupRunExecutor.cpp
    execution/DefaultBackupRunFactory.cpp
    execution/LinkedCancellationMonitor.cpp
    execution/RunExecutionContext.cpp
    execution/RunSessionFactory.cpp
    execution/SystemRunContext.cpp

    execution/actions/BackupRunActionHandler.cpp
    execution/actions/DefaultBackupRunActionHandlerFactory.cpp
    execution/actions/HookActionHandler.cpp
    execution/actions/OwnedBackupRunActionHandler.cpp
    execution/actions/RecoveryActionHandler.cpp
    execution/actions/RepositoryActionHandler.cpp
    execution/actions/RetentionActionHandler.cpp
    execution/actions/SnapshotActionHandler.cpp
    transfer/SnapshotTransfer.cpp
    transfer/TransferCoordinator.cpp
    transfer/TransferSizing.cpp
)
target_compile_features(btrfsbackup-backup-execution PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-backup-execution
    PUBLIC_HEADERS
        backup/execution/BackupActionExecutor.hpp
        backup/execution/BackupRun.hpp
        backup/execution/BackupRunCheckpointPolicy.hpp
        backup/execution/BackupRunExecutor.hpp
        backup/execution/DefaultBackupRunFactory.hpp
        backup/execution/LinkedCancellationMonitor.hpp
        backup/execution/RunExecutionContext.hpp
        backup/execution/RunSessionFactory.hpp
        backup/execution/SystemRunContext.hpp

        backup/execution/actions/BackupRunActionHandler.hpp
        backup/execution/actions/DefaultBackupRunActionHandlerFactory.hpp
        backup/execution/actions/HookActionHandler.hpp
        backup/execution/actions/RecoveryActionHandler.hpp
        backup/execution/actions/RepositoryActionHandler.hpp
        backup/execution/actions/RetentionActionHandler.hpp
        backup/execution/actions/SnapshotActionHandler.hpp

        backup/transfer/SnapshotTransfer.hpp
        backup/transfer/TransferCoordinator.hpp
    PRIVATE_HEADERS
        backup/execution/actions/OwnedBackupRunActionHandler.hpp
        backup/transfer/TransferSizing.hpp
)
target_link_libraries(btrfsbackup-backup-execution
    PUBLIC
        btrfsbackup-backup-ports
    PRIVATE
        btrfsbackup-backup-model
        btrfsbackup-config-domain
        btrfsbackup-core
        btrfsbackup-transfer
)

add_library(btrfsbackup-backup STATIC
    BackupService.cpp
    TargetStorageRecorder.cpp
)
target_compile_features(btrfsbackup-backup PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-backup
    PUBLIC_HEADERS
        backup/BackupService.hpp
        backup/TargetStorageRecorder.hpp
)
target_link_libraries(btrfsbackup-backup
    PUBLIC
        btrfsbackup-backup-execution
        btrfsbackup-config-ports
    PRIVATE
        btrfsbackup-backup-model
        btrfsbackup-config-domain
        btrfsbackup-core
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-backup PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup-execution PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup-model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup-planning PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup-ports PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup-testing PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-transfer PRIVATE -Wall -Wextra -Wpedantic)
endif()
