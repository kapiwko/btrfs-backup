# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-cli STATIC
    backup_tool.cpp
    ctl_tool.cpp
    installation_command.cpp
    profile_command.cpp
    profile_create_command.cpp
    profile_list_command.cpp
    profile_wizard_command.cpp
    runner_command.cpp
    status_command.cpp
    status_history_command.cpp
    status_show_command.cpp
    target_command.cpp
    target_service.cpp
)

target_compile_features(btrfsbackup-cli PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-cli PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup-cli
    PUBLIC
        btrfsbackup-backup
        btrfsbackup-config
        btrfsbackup-platform-linux
        btrfsbackup-state
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()
