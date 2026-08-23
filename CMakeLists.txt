cmake_minimum_required(VERSION 3.20)

project(
    btrfs-backup
    VERSION 0.2.0
    LANGUAGES CXX
)

include(CTest)

find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MOUNT REQUIRED IMPORTED_TARGET mount)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(BTRFSUTIL REQUIRED IMPORTED_TARGET libbtrfsutil)

add_library(btrfsbackup_profile_tool STATIC
    cpp/src/command/installation_command.cpp
    cpp/src/command/profile_create_command.cpp
    cpp/src/command/profile_command.cpp
    cpp/src/command/profile_list_command.cpp
    cpp/src/command/profile_sources_command.cpp
    cpp/src/command/profile_wizard_command.cpp
    cpp/src/command/state_command.cpp
    cpp/src/command/state_fingerprint_command.cpp
    cpp/src/command/state_run_command.cpp
    cpp/src/command/status_command.cpp
    cpp/src/command/status_history_command.cpp
    cpp/src/command/status_show_command.cpp
    cpp/src/command/status_write_command.cpp
    cpp/src/config_fingerprint.cpp
    cpp/src/ctl_tool.cpp
    cpp/src/device_info.cpp
    cpp/src/file_io.cpp
    cpp/src/file_lock.cpp
    cpp/src/identifiers.cpp
    cpp/src/incremental_parent.cpp
    cpp/src/installation_render.cpp
    cpp/src/installation_validate.cpp
    cpp/src/json_io.cpp
    cpp/src/mount_info.cpp
    cpp/src/pending_recovery_plan.cpp
    cpp/src/process.cpp
    cpp/src/profile.cpp
    cpp/src/profile_sources.cpp
    cpp/src/profile_render.cpp
    cpp/src/profile_store.cpp
    cpp/src/profile_wizard_device.cpp
    cpp/src/profile_wizard_install.cpp
    cpp/src/profile_wizard_model.cpp
    cpp/src/profile_wizard_paths.cpp
    cpp/src/profile_wizard_prompt.cpp
    cpp/src/profile_wizard_sources.cpp
    cpp/src/profile_wizard.cpp
    cpp/src/retention_plan.cpp
    cpp/src/run_state.cpp
    cpp/src/shell_env.cpp
    cpp/src/space_check.cpp
    cpp/src/snapshot_inventory.cpp
    cpp/src/status_writer.cpp
    cpp/src/trusted_file.cpp
    cpp/src/validation.cpp
)

target_compile_features(btrfsbackup_profile_tool PUBLIC cxx_std_20)
target_include_directories(btrfsbackup_profile_tool
    PUBLIC
        cpp/include
)
target_link_libraries(btrfsbackup_profile_tool
    PUBLIC
        nlohmann_json::nlohmann_json
        PkgConfig::BTRFSUTIL
        PkgConfig::MOUNT
        PkgConfig::UDEV
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup_profile_tool PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(btrfs-backupctl
    cpp/apps/btrfs-backupctl.cpp
)

target_link_libraries(btrfs-backupctl
    PRIVATE
        btrfsbackup_profile_tool
)

if(BUILD_TESTING)
    add_executable(btrfsbackup-profile-tests
        cpp/tests/profile_tests.cpp
    )

    add_executable(btrfsbackup-device-info-tests
        cpp/tests/device_info_tests.cpp
    )

    add_executable(btrfsbackup-status-history-tests
        cpp/tests/status_history_tests.cpp
    )

    add_executable(btrfsbackup-run-state-tests
        cpp/tests/run_state_tests.cpp
    )

    add_executable(btrfsbackup-profile-command-tests
        cpp/tests/profile_command_tests.cpp
    )

    add_executable(btrfsbackup-installation-tests
        cpp/tests/installation_tests.cpp
    )

    add_executable(btrfsbackup-incremental-parent-tests
        cpp/tests/incremental_parent_tests.cpp
    )

    add_executable(btrfsbackup-pending-recovery-plan-tests
        cpp/tests/pending_recovery_plan_tests.cpp
    )

    add_executable(btrfsbackup-profile-wizard-tests
        cpp/tests/profile_wizard_tests.cpp
    )

    add_executable(btrfsbackup-retention-plan-tests
        cpp/tests/retention_plan_tests.cpp
    )

    add_executable(btrfsbackup-mount-info-tests
        cpp/tests/mount_info_tests.cpp
    )

    add_executable(btrfsbackup-file-lock-tests
        cpp/tests/file_lock_tests.cpp
    )

    add_executable(btrfsbackup-status-writer-tests
        cpp/tests/status_writer_tests.cpp
    )

    add_executable(btrfsbackup-space-check-tests
        cpp/tests/space_check_tests.cpp
    )

    add_executable(btrfsbackup-snapshot-inventory-tests
        cpp/tests/snapshot_inventory_tests.cpp
    )

    add_executable(btrfsbackup-trusted-file-tests
        cpp/tests/trusted_file_tests.cpp
    )

    add_executable(btrfsbackup-validation-tests
        cpp/tests/validation_tests.cpp
    )

    target_link_libraries(btrfsbackup-profile-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-device-info-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-status-history-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-run-state-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-profile-command-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-installation-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-incremental-parent-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-pending-recovery-plan-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-profile-wizard-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-retention-plan-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-mount-info-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-file-lock-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-status-writer-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-space-check-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-snapshot-inventory-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-trusted-file-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-validation-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    add_test(NAME profile-tests COMMAND btrfsbackup-profile-tests)
    add_test(NAME device-info-tests COMMAND btrfsbackup-device-info-tests)
    add_test(NAME status-history-tests COMMAND btrfsbackup-status-history-tests)
    add_test(NAME run-state-tests COMMAND btrfsbackup-run-state-tests)
    add_test(NAME profile-command-tests COMMAND btrfsbackup-profile-command-tests)
    add_test(NAME installation-tests COMMAND btrfsbackup-installation-tests)
    add_test(NAME incremental-parent-tests COMMAND btrfsbackup-incremental-parent-tests)
    add_test(NAME pending-recovery-plan-tests COMMAND btrfsbackup-pending-recovery-plan-tests)
    add_test(NAME profile-wizard-tests COMMAND btrfsbackup-profile-wizard-tests)
    add_test(NAME retention-plan-tests COMMAND btrfsbackup-retention-plan-tests)
    add_test(NAME file-lock-tests COMMAND btrfsbackup-file-lock-tests)
    add_test(NAME mount-info-tests COMMAND btrfsbackup-mount-info-tests)
    add_test(NAME space-check-tests COMMAND btrfsbackup-space-check-tests)
    add_test(NAME snapshot-inventory-tests COMMAND btrfsbackup-snapshot-inventory-tests)
    add_test(NAME status-writer-tests COMMAND btrfsbackup-status-writer-tests)
    add_test(NAME trusted-file-tests COMMAND btrfsbackup-trusted-file-tests)
    add_test(NAME validation-tests COMMAND btrfsbackup-validation-tests)
endif()
