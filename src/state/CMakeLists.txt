# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-state-model STATIC
    run_status.cpp
)

target_compile_features(btrfsbackup-state-model PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-state-model
    PUBLIC_HEADERS
        state/run_status.hpp
)
target_link_libraries(btrfsbackup-state-model PUBLIC btrfsbackup-core)

add_library(btrfsbackup-state STATIC
    serialization.cpp
    file_pending_marker_store.cpp
    file_run_state_repository.cpp
    run_checkpoint_store.cpp
    run_history.cpp
    run_state.cpp
    run_status_projection.cpp
    status_service.cpp
    status_writer.cpp
)

target_compile_features(btrfsbackup-state PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-state
    PUBLIC_HEADERS
        state/file_pending_marker_store.hpp
        state/file_run_state_repository.hpp
        state/run_checkpoint_store.hpp
        state/run_history.hpp
        state/run_state.hpp
        state/run_status_projection.hpp
        state/serialization.hpp
        state/status_service.hpp
        state/status_writer.hpp
)
target_link_libraries(btrfsbackup-state
    PUBLIC
        btrfsbackup-config-model
        btrfsbackup-backup-ports
        btrfsbackup-core
        btrfsbackup-state-model
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-state-model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-state PRIVATE -Wall -Wextra -Wpedantic)
endif()
