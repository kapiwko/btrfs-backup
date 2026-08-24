add_library(btrfsbackup-engine STATIC
    ../src/engine/backup_run_action_effects.cpp
    ../src/engine/backup_run_executor.cpp
    ../src/engine/backup_run_persistence.cpp
    ../src/engine/backup_run_plan.cpp
    ../src/engine/incremental_parent.cpp
    ../src/engine/pending_recovery_plan.cpp
    ../src/engine/snapshot_transfer.cpp
    ../src/engine/status_writer.cpp
    ../src/engine/transfer_pipeline.cpp
)

target_compile_features(btrfsbackup-engine PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-engine
    PUBLIC
        ../include
)
target_link_libraries(btrfsbackup-engine
    PUBLIC
        btrfsbackup-model
        btrfsbackup-system
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
