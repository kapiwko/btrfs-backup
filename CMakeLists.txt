cmake_minimum_required(VERSION 3.20)

project(
    btrfs-backup
    VERSION 0.2.0
    LANGUAGES CXX
)

include(CTest)

find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(BLKID REQUIRED IMPORTED_TARGET blkid)
pkg_check_modules(MOUNT REQUIRED IMPORTED_TARGET mount)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(BTRFSUTIL REQUIRED IMPORTED_TARGET libbtrfsutil)

add_library(btrfsbackup-model STATIC
    cpp/src/config_fingerprint.cpp
    cpp/src/identifiers.cpp
    cpp/src/json_io.cpp
    cpp/src/profile.cpp
    cpp/src/profile_sources.cpp
    cpp/src/profile_wizard_model.cpp
    cpp/src/retention_plan.cpp
    cpp/src/snapshot_inventory.cpp
    cpp/src/validation.cpp
)

target_compile_features(btrfsbackup-model PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-model
    PUBLIC
        cpp/include
)
target_link_libraries(btrfsbackup-model
    PUBLIC
        nlohmann_json::nlohmann_json
)

add_library(btrfsbackup-system STATIC
    cpp/src/btrfs_operations.cpp
    cpp/src/device_info.cpp
    cpp/src/file_io.cpp
    cpp/src/file_lock.cpp
    cpp/src/mount_info.cpp
    cpp/src/process.cpp
    cpp/src/profile_loader.cpp
    cpp/src/run_state.cpp
    cpp/src/shell_env.cpp
    cpp/src/space_check.cpp
    cpp/src/target_mount_validation.cpp
    cpp/src/trusted_file.cpp
)

target_compile_features(btrfsbackup-system PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-system
    PUBLIC
        cpp/include
)
target_link_libraries(btrfsbackup-system
    PUBLIC
        btrfsbackup-model
        PkgConfig::BLKID
        PkgConfig::BTRFSUTIL
        PkgConfig::MOUNT
        PkgConfig::UDEV
)

add_library(btrfsbackup-engine STATIC
    cpp/src/backup_run_action_effects.cpp
    cpp/src/backup_run_executor.cpp
    cpp/src/backup_run_persistence.cpp
    cpp/src/backup_run_plan.cpp
    cpp/src/incremental_parent.cpp
    cpp/src/pending_recovery_plan.cpp
    cpp/src/snapshot_transfer.cpp
    cpp/src/status_writer.cpp
    cpp/src/transfer_pipeline.cpp
)

target_compile_features(btrfsbackup-engine PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-engine
    PUBLIC
        cpp/include
)
target_link_libraries(btrfsbackup-engine
    PUBLIC
        btrfsbackup-model
        btrfsbackup-system
)

add_library(btrfsbackup-core STATIC
    cpp/src/command/installation_command.cpp
    cpp/src/command/profile_create_command.cpp
    cpp/src/command/profile_command.cpp
    cpp/src/command/profile_list_command.cpp
    cpp/src/command/profile_sources_command.cpp
    cpp/src/command/profile_wizard_command.cpp
    cpp/src/command/runner_command.cpp
    cpp/src/command/state_command.cpp
    cpp/src/command/state_fingerprint_command.cpp
    cpp/src/command/state_run_command.cpp
    cpp/src/command/status_command.cpp
    cpp/src/command/status_history_command.cpp
    cpp/src/command/status_show_command.cpp
    cpp/src/command/status_write_command.cpp
    cpp/src/command/target_command.cpp
    cpp/src/ctl_tool.cpp
    cpp/src/installation_render.cpp
    cpp/src/installation_validate.cpp
    cpp/src/profile_render.cpp
    cpp/src/profile_store.cpp
    cpp/src/profile_wizard.cpp
    cpp/src/profile_wizard_device.cpp
    cpp/src/profile_wizard_install.cpp
    cpp/src/profile_wizard_paths.cpp
    cpp/src/profile_wizard_prompt.cpp
    cpp/src/profile_wizard_sources.cpp
    cpp/src/runtime_adapters.cpp
)

target_compile_features(btrfsbackup-core PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-core
    PUBLIC
        cpp/include
)
target_link_libraries(btrfsbackup-core
    PUBLIC
        btrfsbackup-model
        btrfsbackup-system
        btrfsbackup-engine
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-system PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-engine PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(btrfs-backupctl
    cpp/apps/btrfs-backupctl.cpp
)

target_link_libraries(btrfs-backupctl
    PRIVATE
        btrfsbackup-core
)

if(BUILD_TESTING)
    function(add_btrfsbackup_test target source test_name)
        add_executable(${target}
            ${source}
        )
        target_link_libraries(${target}
            PRIVATE
                btrfsbackup-core
        )
        add_test(NAME ${test_name} COMMAND ${target})
    endfunction()

    add_btrfsbackup_test(btrfsbackup-backup-run-plan-tests cpp/tests/backup_run_plan_tests.cpp backup-run-plan-tests)
    add_btrfsbackup_test(btrfsbackup-backup-run-executor-tests cpp/tests/backup_run_executor_tests.cpp backup-run-executor-tests)
    add_btrfsbackup_test(btrfsbackup-backup-run-action-effects-tests cpp/tests/backup_run_action_effects_tests.cpp backup-run-action-effects-tests)
    add_btrfsbackup_test(btrfsbackup-backup-run-persistence-tests cpp/tests/backup_run_persistence_tests.cpp backup-run-persistence-tests)
    add_btrfsbackup_test(btrfsbackup-btrfs-operations-tests cpp/tests/btrfs_operations_tests.cpp btrfs-operations-tests)
    add_btrfsbackup_test(btrfsbackup-profile-tests cpp/tests/profile_tests.cpp profile-tests)
    add_btrfsbackup_test(btrfsbackup-profile-schema-contract-tests cpp/tests/profile_schema_contract_tests.cpp profile-schema-contract-tests)
    add_btrfsbackup_test(btrfsbackup-device-info-tests cpp/tests/device_info_tests.cpp device-info-tests)
    add_btrfsbackup_test(btrfsbackup-status-history-tests cpp/tests/status_history_tests.cpp status-history-tests)
    add_btrfsbackup_test(btrfsbackup-run-state-tests cpp/tests/run_state_tests.cpp run-state-tests)
    add_btrfsbackup_test(btrfsbackup-profile-command-tests cpp/tests/profile_command_tests.cpp profile-command-tests)
    add_btrfsbackup_test(btrfsbackup-installation-tests cpp/tests/installation_tests.cpp installation-tests)
    add_btrfsbackup_test(btrfsbackup-incremental-parent-tests cpp/tests/incremental_parent_tests.cpp incremental-parent-tests)
    add_btrfsbackup_test(btrfsbackup-pending-recovery-plan-tests cpp/tests/pending_recovery_plan_tests.cpp pending-recovery-plan-tests)
    add_btrfsbackup_test(btrfsbackup-profile-wizard-tests cpp/tests/profile_wizard_tests.cpp profile-wizard-tests)
    add_btrfsbackup_test(btrfsbackup-retention-plan-tests cpp/tests/retention_plan_tests.cpp retention-plan-tests)
    add_btrfsbackup_test(btrfsbackup-runtime-adapters-tests cpp/tests/runtime_adapters_tests.cpp runtime-adapters-tests)
    add_btrfsbackup_test(btrfsbackup-runner-command-tests cpp/tests/runner_command_tests.cpp runner-command-tests)
    add_btrfsbackup_test(btrfsbackup-mount-info-tests cpp/tests/mount_info_tests.cpp mount-info-tests)
    add_btrfsbackup_test(btrfsbackup-file-lock-tests cpp/tests/file_lock_tests.cpp file-lock-tests)
    add_btrfsbackup_test(btrfsbackup-status-writer-tests cpp/tests/status_writer_tests.cpp status-writer-tests)
    add_btrfsbackup_test(btrfsbackup-space-check-tests cpp/tests/space_check_tests.cpp space-check-tests)
    add_btrfsbackup_test(btrfsbackup-snapshot-inventory-tests cpp/tests/snapshot_inventory_tests.cpp snapshot-inventory-tests)
    add_btrfsbackup_test(btrfsbackup-snapshot-transfer-tests cpp/tests/snapshot_transfer_tests.cpp snapshot-transfer-tests)
    add_btrfsbackup_test(btrfsbackup-target-command-tests cpp/tests/target_command_tests.cpp target-command-tests)
    add_btrfsbackup_test(btrfsbackup-target-mount-validation-tests cpp/tests/target_mount_validation_tests.cpp target-mount-validation-tests)
    add_btrfsbackup_test(btrfsbackup-trusted-file-tests cpp/tests/trusted_file_tests.cpp trusted-file-tests)
    add_btrfsbackup_test(btrfsbackup-transfer-pipeline-tests cpp/tests/transfer_pipeline_tests.cpp transfer-pipeline-tests)
    add_btrfsbackup_test(btrfsbackup-validation-tests cpp/tests/validation_tests.cpp validation-tests)
endif()
