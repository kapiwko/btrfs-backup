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
    systemd_target_manager.cpp
    systemd_unit.cpp
    termination_signal_monitor.cpp
    trusted_directory.cpp
    trusted_executable.cpp
    trusted_file.cpp
)

target_compile_features(btrfsbackup-platform-linux PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-platform-linux
    PUBLIC_HEADERS
        platform/linux/btrfs_util_operations.hpp
        platform/linux/device_info.hpp
        platform/linux/file_backup_run_lease_provider.hpp
        platform/linux/file_io.hpp
        platform/linux/file_lock.hpp
        platform/linux/mount_info.hpp
        platform/linux/posix_cancellation_signal.hpp
        platform/linux/posix_command_runner.hpp
        platform/linux/posix_filesystem.hpp
        platform/linux/posix_transfer_pipeline.hpp
        platform/linux/posix_transfer_process.hpp
        platform/linux/posix_transfer_pump.hpp
        platform/linux/process.hpp
        platform/linux/process_spawn.hpp
        platform/linux/safe_directory_root.hpp
        platform/linux/space_check.hpp
        platform/linux/splice.hpp
        platform/linux/system_configuration_activator.hpp
        platform/linux/systemd_target_manager.hpp
        platform/linux/systemd_unit.hpp
        platform/linux/termination_signal_monitor.hpp
        platform/linux/trusted_directory.hpp
        platform/linux/trusted_executable.hpp
        platform/linux/trusted_file.hpp
)
target_link_libraries(btrfsbackup-platform-linux
    PUBLIC
        btrfsbackup-backup-model
        btrfsbackup-backup-ports
        btrfsbackup-core
        btrfsbackup-transfer
        btrfsbackup-config-domain
        btrfsbackup-config-ports
        PkgConfig::BLKID
        PkgConfig::BTRFSUTIL
        PkgConfig::MOUNT
        PkgConfig::UDEV
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-platform-linux PRIVATE -Wall -Wextra -Wpedantic)
endif()
