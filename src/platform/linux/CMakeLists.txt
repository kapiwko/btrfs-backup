# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-platform-linux STATIC
    btrfs_util_operations.cpp
    device_info.cpp
    file_io.cpp
    file_backup_run_lease_provider.cpp
    file_lock.cpp
    mount_info.cpp
    posix_cancellation_signal.cpp
    posix_command_runner.cpp
    posix_filesystem.cpp
    posix_transfer_pipeline.cpp
    posix_transfer_process.cpp
    posix_transfer_pump.cpp
    process.cpp
    process_spawn.cpp
    safe_directory_root.cpp
    space_check.cpp
    splice.cpp
    system_configuration_activator.cpp
    systemd_target_mounter.cpp
    termination_signal_monitor.cpp
    trusted_directory.cpp
    trusted_executable.cpp
    trusted_file.cpp
)

target_compile_features(btrfsbackup-platform-linux PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-platform-linux PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup-platform-linux
    PUBLIC
        btrfsbackup-backup-model
        btrfsbackup-backup-ports
        btrfsbackup-core
        btrfsbackup-transfer
        btrfsbackup-config-model
        PkgConfig::BLKID
        PkgConfig::BTRFSUTIL
        PkgConfig::MOUNT
        PkgConfig::UDEV
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-platform-linux PRIVATE -Wall -Wextra -Wpedantic)
endif()
