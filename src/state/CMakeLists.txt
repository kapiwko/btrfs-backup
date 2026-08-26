# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-state-model STATIC
    run_status.cpp
)

target_compile_features(btrfsbackup-state-model PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-state-model PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup-state-model PUBLIC btrfsbackup-identifier-model)

add_library(btrfsbackup-state STATIC
    backup_run_serialization.cpp
    config_fingerprint.cpp
    run_checkpoint_store.cpp
    run_state.cpp
    run_status_projection.cpp
    status_service.cpp
    status_writer.cpp
)

target_compile_features(btrfsbackup-state PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-state PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup-state
    PUBLIC
        btrfsbackup-config-model
        btrfsbackup-platform-linux
        btrfsbackup-state-model
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-state-model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-state PRIVATE -Wall -Wextra -Wpedantic)
endif()
