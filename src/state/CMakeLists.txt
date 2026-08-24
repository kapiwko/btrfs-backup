add_library(btrfsbackup-state-model STATIC
    run_status.cpp
)

target_compile_features(btrfsbackup-state-model PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-state-model PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_library(btrfsbackup-state STATIC
    backup_run_persistence.cpp
    config_fingerprint.cpp
    run_state.cpp
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
