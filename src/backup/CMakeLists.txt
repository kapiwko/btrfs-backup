# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-backup-model STATIC
    model/retention_plan.cpp
    model/snapshot_inventory.cpp
)

target_compile_features(btrfsbackup-backup-model PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-backup-model
    PUBLIC_HEADERS
        backup/model/retention_plan.hpp
        backup/model/snapshot_inventory.hpp
)
target_link_libraries(btrfsbackup-backup-model PUBLIC btrfsbackup-config-model)

add_library(btrfsbackup-transfer STATIC
    transfer/async_transfer.cpp
    transfer/transfer_event.cpp
    transfer/transfer_result.cpp
    transfer/transfer_speed_estimator.cpp
)

target_compile_features(btrfsbackup-transfer PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-transfer PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup-transfer
    PUBLIC
        btrfsbackup-core
        btrfsbackup-config-model
)

add_library(btrfsbackup-backup-ports STATIC
    ports/btrfs_operations.cpp
    ports/command_runner.cpp
    ports/mount_inspector.cpp
)

target_compile_features(btrfsbackup-backup-ports PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-backup-ports PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup-backup-ports
    PUBLIC
        btrfsbackup-backup-model
        btrfsbackup-config-model
        btrfsbackup-transfer
)

add_library(btrfsbackup-backup-domain STATIC
    incremental_parent.cpp
    target_mount_validation.cpp
)

target_compile_features(btrfsbackup-backup-domain PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-backup-domain PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup-backup-domain
    PUBLIC
        btrfsbackup-backup-model
        btrfsbackup-backup-ports
        btrfsbackup-config-model
)

add_library(btrfsbackup-backup STATIC
    backup_run.cpp
    backup_run_executor.cpp
    backup_service.cpp
    default_backup_planner.cpp
    default_backup_run_factory.cpp
    system_run_context.cpp
    action_handlers/backup_run_action_handler.cpp
    action_handlers/hook_action_handler.cpp
    action_handlers/recovery_action_handler.cpp
    action_handlers/repository_action_handler.cpp
    action_handlers/retention_action_handler.cpp
    action_handlers/snapshot_action_handler.cpp
    action_handlers/transfer_action_handler.cpp
    model/backup_run_actions.cpp
    model/backup_run_plan.cpp
    pending_recovery_plan.cpp
    snapshot_transfer.cpp
)

target_compile_features(btrfsbackup-backup PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-backup PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup-backup
    PUBLIC
        btrfsbackup-backup-model
        btrfsbackup-backup-domain
        btrfsbackup-backup-ports
        btrfsbackup-transfer
        btrfsbackup-config-ports
        btrfsbackup-state
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-backup-model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-transfer PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup-ports PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup-domain PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup PRIVATE -Wall -Wextra -Wpedantic)
endif()
