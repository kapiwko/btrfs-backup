cmake_minimum_required(VERSION 3.20)

project(
    btrfs-backup
    VERSION 0.2.0
    LANGUAGES CXX
)

include(CTest)

find_package(nlohmann_json REQUIRED)

add_library(btrfsbackup_profile_tool STATIC
    cpp/src/command/installation_command.cpp
    cpp/src/command/profile_create_command.cpp
    cpp/src/command/profile_command.cpp
    cpp/src/command/run_state_command.cpp
    cpp/src/command/state_command.cpp
    cpp/src/command/status_command.cpp
    cpp/src/command/status_write_command.cpp
    cpp/src/config_fingerprint.cpp
    cpp/src/ctl_tool.cpp
    cpp/src/file_io.cpp
    cpp/src/history.cpp
    cpp/src/identifiers.cpp
    cpp/src/installation_render.cpp
    cpp/src/installation_validate.cpp
    cpp/src/json_io.cpp
    cpp/src/migrate_profile.cpp
    cpp/src/process.cpp
    cpp/src/profile.cpp
    cpp/src/profile_list.cpp
    cpp/src/profile_render.cpp
    cpp/src/profile_store.cpp
    cpp/src/run_state.cpp
    cpp/src/shell_env.cpp
    cpp/src/source_definition.cpp
    cpp/src/status.cpp
    cpp/src/status_writer.cpp
)

target_compile_features(btrfsbackup_profile_tool PUBLIC cxx_std_20)
target_include_directories(btrfsbackup_profile_tool
    PUBLIC
        cpp/include
)
target_link_libraries(btrfsbackup_profile_tool
    PUBLIC
        nlohmann_json::nlohmann_json
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

    add_executable(btrfsbackup-status-writer-tests
        cpp/tests/status_writer_tests.cpp
    )

    target_link_libraries(btrfsbackup-profile-tests
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

    target_link_libraries(btrfsbackup-status-writer-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    add_test(NAME profile-tests COMMAND btrfsbackup-profile-tests)
    add_test(NAME status-history-tests COMMAND btrfsbackup-status-history-tests)
    add_test(NAME run-state-tests COMMAND btrfsbackup-run-state-tests)
    add_test(NAME profile-command-tests COMMAND btrfsbackup-profile-command-tests)
    add_test(NAME installation-tests COMMAND btrfsbackup-installation-tests)
    add_test(NAME status-writer-tests COMMAND btrfsbackup-status-writer-tests)
endif()
