add_library(btrfsbackup-system STATIC
    ../src/system/application_paths.cpp
    ../src/system/btrfs_operations.cpp
    ../src/system/device_info.cpp
    ../src/system/file_io.cpp
    ../src/system/file_lock.cpp
    ../src/system/mount_info.cpp
    ../src/system/process.cpp
    ../src/system/process_spawn.cpp
    ../src/system/profile_loader.cpp
    ../src/system/run_state.cpp
    ../src/system/safe_directory_root.cpp
    ../src/system/space_check.cpp
    ../src/system/target_mount_validation.cpp
    ../src/system/trusted_executable.cpp
    ../src/system/trusted_file.cpp
)

target_compile_features(btrfsbackup-system PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-system
    PUBLIC
        ../include
)
target_link_libraries(btrfsbackup-system
    PUBLIC
        btrfsbackup-model
        PkgConfig::BLKID
        PkgConfig::BTRFSUTIL
        PkgConfig::MOUNT
        PkgConfig::UDEV
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-system PRIVATE -Wall -Wextra -Wpedantic)
endif()
