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
)

target_compile_features(btrfsbackup-backup-model PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-backup-model
    PUBLIC_HEADERS
        backup/model/BackupPlanningSnapshot.hpp
        backup/model/BackupExecution.hpp
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
)
target_link_libraries(btrfsbackup-backup-model
    PUBLIC
        btrfsbackup-config-domain
    PRIVATE
        btrfsbackup-core
)

add_library(btrfsbackup-transfer STATIC
    transfer/AsyncTransfer.cpp
    transfer/TransferEvent.cpp
    transfer/TransferResult.cpp
    transfer/TransferSpeedEstimator.cpp
)

target_compile_features(btrfsbackup-transfer PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-transfer
    PUBLIC_HEADERS
        backup/transfer/AsyncTransfer.hpp
        backup/transfer/TransferEvent.hpp
        backup/transfer/ITransferPipeline.hpp
        backup/transfer/TransferPlan.hpp
        backup/transfer/TransferResult.hpp
        backup/transfer/TransferSpeedEstimator.hpp
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

target_compile_features(btrfsbackup-backup-ports PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-backup-ports
    PUBLIC_HEADERS
        backup/ports/IBackupDiscovery.hpp
        backup/ports/IBackupPlanBuilder.hpp
        backup/ports/IBackupPreflight.hpp
        backup/ports/IBackupRunCheckpointStore.hpp
        backup/ports/IBackupRunEventSink.hpp
        backup/ports/BackupRunActionHandlerFactory.hpp
        backup/ports/IBackupRunFactory.hpp
        backup/ports/IBtrfsOperations.hpp
        backup/ports/CancellationRequestStore.hpp
        backup/ports/CancellationMonitor.hpp
        backup/ports/ICheckpointStoreFactory.hpp
        backup/ports/ICommandRunner.hpp
        backup/ports/IFileSystem.hpp
        backup/ports/IMountInspector.hpp
        backup/ports/IPendingMarkerStore.hpp
        backup/ports/Process.hpp
        backup/ports/RunContext.hpp
        backup/ports/IRunEventSinkFactory.hpp
        backup/ports/IRunLedger.hpp
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

target_compile_features(btrfsbackup-backup-testing PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-backup-testing
    PUBLIC_HEADERS
        backup/testing/NullBackupRunEventSink.hpp
)
target_link_libraries(btrfsbackup-backup-testing
    PUBLIC
        btrfsbackup-backup-ports
)

add_library(btrfsbackup-backup STATIC
    BackupActionExecutor.cpp
    BackupPreflight.cpp
    BackupPreflightValidation.cpp
    BackupRun.cpp
    BackupRunCheckpointPolicy.cpp
    BackupRunExecutor.cpp
    BackupService.cpp
    BackupDiscovery.cpp
    BackupPlanBuilder.cpp
    DefaultBackupRunActionHandlerFactory.cpp
    DefaultBackupRunFactory.cpp
    LinkedCancellationMonitor.cpp
    RunExecutionContext.cpp
    RunSessionFactory.cpp
    SystemRunContext.cpp
    action_handlers/BackupRunActionHandler.cpp
    action_handlers/HookActionHandler.cpp
    action_handlers/RecoveryActionHandler.cpp
    action_handlers/RepositoryActionHandler.cpp
    action_handlers/RetentionActionHandler.cpp
    action_handlers/SnapshotActionHandler.cpp
    SnapshotTransfer.cpp
    transfer/TransferCoordinator.cpp
)

target_compile_features(btrfsbackup-backup PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-backup
    PUBLIC_HEADERS
        backup/action_handlers/BackupRunActionHandler.hpp
        backup/action_handlers/HookActionHandler.hpp
        backup/action_handlers/RecoveryActionHandler.hpp
        backup/action_handlers/RepositoryActionHandler.hpp
        backup/action_handlers/RetentionActionHandler.hpp
        backup/action_handlers/SnapshotActionHandler.hpp
        backup/BackupActionExecutor.hpp
        backup/BackupDiscovery.hpp
        backup/BackupPlanBuilder.hpp
        backup/BackupPreflight.hpp
        backup/BackupPreflightValidation.hpp
        backup/BackupRun.hpp
        backup/BackupRunCheckpointPolicy.hpp
        backup/BackupRunExecutor.hpp
        backup/BackupService.hpp
        backup/DefaultBackupRunActionHandlerFactory.hpp
        backup/DefaultBackupRunFactory.hpp
        backup/LinkedCancellationMonitor.hpp
        backup/RunExecutionContext.hpp
        backup/RunSessionFactory.hpp
        backup/SnapshotTransfer.hpp
        backup/SystemRunContext.hpp
        backup/transfer/TransferCoordinator.hpp
)
target_link_libraries(btrfsbackup-backup
    PUBLIC
        btrfsbackup-backup-ports
        btrfsbackup-config-ports
    PRIVATE
        btrfsbackup-backup-model
        btrfsbackup-config-domain
        btrfsbackup-core
        btrfsbackup-transfer
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-backup-model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-transfer PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup-ports PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup-testing PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup PRIVATE -Wall -Wextra -Wpedantic)
endif()
