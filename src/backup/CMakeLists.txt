add_library(btrfsbackup-backup-model STATIC
    retention_plan.cpp
    snapshot_inventory.cpp
)

target_compile_features(btrfsbackup-backup-model PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-backup-model PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup-backup-model PUBLIC btrfsbackup-config-model)

add_library(btrfsbackup-backup STATIC
    backup_run_action_effects.cpp
    backup_run_executor.cpp
    backup_run_persistence.cpp
    backup_run_plan.cpp
    backup_service.cpp
    incremental_parent.cpp
    pending_recovery_plan.cpp
    snapshot_transfer.cpp
    target_mount_validation.cpp
    target_service.cpp
    transfer_pipeline.cpp
)

target_compile_features(btrfsbackup-backup PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-backup PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup-backup
    PUBLIC
        btrfsbackup-backup-model
        btrfsbackup-config
        btrfsbackup-platform-linux
        btrfsbackup-state
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-backup-model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-backup PRIVATE -Wall -Wextra -Wpedantic)
endif()
