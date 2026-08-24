add_library(btrfsbackup-cli STATIC
    ../src/cli/backup_tool.cpp
    ../src/cli/command/installation_command.cpp
    ../src/cli/command/profile_create_command.cpp
    ../src/cli/command/profile_command.cpp
    ../src/cli/command/profile_list_command.cpp
    ../src/cli/command/profile_wizard_command.cpp
    ../src/cli/command/runner_command.cpp
    ../src/cli/command/status_command.cpp
    ../src/cli/command/status_history_command.cpp
    ../src/cli/command/status_show_command.cpp
    ../src/cli/command/target_command.cpp
    ../src/cli/ctl_tool.cpp
)

target_compile_features(btrfsbackup-cli PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-cli
    PUBLIC
        ../include
)
target_link_libraries(btrfsbackup-cli
    PUBLIC
        btrfsbackup-application
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(btrfs-backupctl
    ../apps/btrfs-backupctl.cpp
)
target_link_libraries(btrfs-backupctl
    PRIVATE
        btrfsbackup-cli
)

add_executable(btrfs-backup-native
    ../apps/btrfs-backup.cpp
)
set_target_properties(btrfs-backup-native PROPERTIES OUTPUT_NAME btrfs-backup)
target_link_libraries(btrfs-backup-native
    PRIVATE
        btrfsbackup-cli
)
