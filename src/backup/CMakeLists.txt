# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-backup-model STATIC
    model/backup_planning_snapshot.cpp
    model/backup_run_actions.cpp
    model/backup_run_plan.cpp
    model/incremental_parent.cpp
    model/pending_recovery.cpp
    model/retention_plan.cpp
    model/snapshot_inventory.cpp
)

target_compile_features(btrfsbackup-backup-model PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-backup-model
    PUBLIC_HEADERS
        backup/model/backup_planning_snapshot.hpp
        backup/model/backup_execution.hpp
        backup/model/backup_run_action.hpp
        backup/model/backup_run_actions.hpp
        backup/model/backup_run_event.hpp
        backup/model/backup_run_execution.hpp
        backup/model/backup_run_plan.hpp
        backup/model/incremental_parent.hpp
        backup/model/pending_recovery.hpp
        backup/model/retention_plan.hpp
        backup/model/snapshot.hpp
        backup/model/snapshot_inventory.hpp
)
target_link_libraries(btrfsbackup-backup-model PUBLIC btrfsbackup-config-domain)

add_library(btrfsbackup-transfer STATIC
    transfer/async_transfer.cpp
    transfer/transfer_event.cpp
    transfer/transfer_result.cpp
    transfer/transfer_speed_estimator.cpp
)

target_compile_features(btrfsbackup-transfer PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-transfer
    PUBLIC_HEADERS
        backup/transfer/async_transfer.hpp
        backup/transfer/transfer_event.hpp
        backup/transfer/transfer_pipeline.hpp
        backup/transfer/transfer_plan.hpp
        backup/transfer/transfer_result.hpp
        backup/transfer/transfer_speed_estimator.hpp
)
target_link_libraries(btrfsbackup-transfer
    PUBLIC
        btrfsbackup-core
        btrfsbackup-config-domain
)

add_library(btrfsbackup-backup-ports STATIC
    ports/btrfs_operations.cpp
    ports/command_runner.cpp
    ports/mount_inspector.cpp
)

target_compile_features(btrfsbackup-backup-ports PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-backup-ports
    PUBLIC_HEADERS
        backup/ports/backup_discovery.hpp
        backup/ports/backup_plan_builder.hpp
        backup/ports/backup_run_factory.hpp
        backup/ports/btrfs_operations.hpp
        backup/ports/cancellation_request_store.hpp
        backup/ports/cancellation_monitor.hpp
        backup/ports/checkpoint_store_factory.hpp
        backup/ports/command_runner.hpp
        backup/ports/filesystem.hpp
        backup/ports/mount_inspector.hpp
        backup/ports/pending_marker_store.hpp
        backup/ports/process.hpp
        backup/ports/run_context.hpp
        backup/ports/run_event_sink_factory.hpp
        backup/ports/run_ledger.hpp
        backup/ports/run_lease.hpp
        backup/ports/safe_directory.hpp
        backup/ports/target_manager.hpp
        backup/ports/trusted_executable.hpp
)
target_link_libraries(btrfsbackup-backup-ports
    PUBLIC
        btrfsbackup-backup-model
        btrfsbackup-config-domain
        btrfsbackup-transfer
)

add_library(btrfsbackup-backup-domain STATIC
    target_mount_validation.cpp
)

target_compile_features(btrfsbackup-backup-domain PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-backup-domain
    PUBLIC_HEADERS
        backup/target_mount_validation.hpp
)
target_link_libraries(btrfsbackup-backup-domain
    PUBLIC
        btrfsbackup-backup-model
        btrfsbackup-backup-ports
        btrfsbackup-config-domain
)

add_library(btrfsbackup-backup STATIC
    backup_run.cpp
    backup_run_checkpoint_policy.cpp
    backup_run_executor.cpp
    backup_service.cpp
    backup_discovery.cpp
    backup_plan_builder.cpp
    default_backup_run_factory.cpp
    linked_cancellation_monitor.cpp
    run_execution_context.cpp
    system_run_context.cpp
    action_handlers/backup_run_action_handler.cpp
    action_handlers/hook_action_handler.cpp
    action_handlers/recovery_action_handler.cpp
    action_handlers/repository_action_handler.cpp
    action_handlers/retention_action_handler.cpp
    action_handlers/snapshot_action_handler.cpp
    snapshot_transfer.cpp
    transfer/transfer_coordinator.cpp
)

target_compile_features(btrfsbackup-backup PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-backup
    PUBLIC_HEADERS
        backup/action_handlers/backup_run_action_handler.hpp
        backup/action_handlers/hook_action_handler.hpp
        backup/action_handlers/recovery_action_handler.hpp
        backup/action_handlers/repository_action_handler.hpp
        backup/action_handlers/retention_action_handler.hpp
        backup/action_handlers/snapshot_action_handler.hpp
        backup/backup_discovery.hpp
        backup/backup_plan_builder.hpp
        backup/backup_run.hpp
        backup/backup_run_checkpoint_policy.hpp
        backup/backup_run_executor.hpp
        backup/backup_service.hpp
        backup/default_backup_run_factory.hpp
        backup/linked_cancellation_monitor.hpp
        backup/run_execution_context.hpp
        backup/snapshot_transfer.hpp
        backup/system_run_context.hpp
        backup/transfer/transfer_coordinator.hpp
)
target_link_libraries(btrfsbackup-backup
    PUBLIC
        btrfsbackup-backup-model
        btrfsbackup-backup-domain
        btrfsbackup-backup-ports
        btrfsbackup-transfer
        btrfsbackup-config-ports
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-backup-model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-transfer PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup-ports PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup-domain PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup PRIVATE -Wall -Wextra -Wpedantic)
endif()
