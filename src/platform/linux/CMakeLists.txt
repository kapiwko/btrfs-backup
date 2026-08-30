# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-platform-linux STATIC
    LibBtrfsOperations.cpp
    DeviceInfo.cpp
    FileIo.cpp
    FileBackupRunLeaseProvider.cpp
    FileLock.cpp
    MountInfo.cpp
    PosixCancellationSignal.cpp
    PosixCommandRunner.cpp
    PosixDurableFileOperations.cpp
    PosixFileSystem.cpp
    PosixTransferPipeline.cpp
    PosixTransferSession.cpp
    PosixTransferProcess.cpp
    PosixTransferPump.cpp
    Process.cpp
    ProcessSpawn.cpp
    SafeDirectoryRoot.cpp
    SpaceCheck.cpp
    Splice.cpp
    LinuxSystemConfigurationActivator.cpp
    SystemdTargetManager.cpp
    SystemdMountedTargetSession.cpp
    SystemdUnit.cpp
    TerminationSignalMonitor.cpp
    TrustedDirectory.cpp
    TrustedExecutable.cpp
    TrustedFile.cpp
)

target_compile_features(btrfsbackup-platform-linux PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-platform-linux
    PUBLIC_HEADERS
        platform/linux/LibBtrfsOperations.hpp
        platform/linux/DeviceInfo.hpp
        platform/linux/FileBackupRunLeaseProvider.hpp
        platform/linux/FileIo.hpp
        platform/linux/FileLock.hpp
        platform/linux/MountInfo.hpp
        platform/linux/PosixCancellationSignal.hpp
        platform/linux/PosixCommandRunner.hpp
        platform/linux/PosixDurableFileOperations.hpp
        platform/linux/PosixFileSystem.hpp
        platform/linux/PosixTransferPipeline.hpp
        platform/linux/PosixTransferSession.hpp
        platform/linux/PosixTransferProcess.hpp
        platform/linux/PosixTransferPump.hpp
        platform/linux/Process.hpp
        platform/linux/ProcessSpawn.hpp
        platform/linux/SafeDirectoryRoot.hpp
        platform/linux/SpaceCheck.hpp
        platform/linux/Splice.hpp
        platform/linux/LinuxSystemConfigurationActivator.hpp
        platform/linux/SystemdTargetManager.hpp
        platform/linux/SystemdUnit.hpp
        platform/linux/TerminationSignalMonitor.hpp
        platform/linux/TrustedDirectory.hpp
        platform/linux/TrustedExecutable.hpp
        platform/linux/TrustedFile.hpp
    PRIVATE_HEADERS
        platform/linux/SystemdMountedTargetSession.hpp
)
target_link_libraries(btrfsbackup-platform-linux
    PUBLIC
        btrfsbackup-backup-ports
        btrfsbackup-state-persistence-ports
        btrfsbackup-config-ports
    PRIVATE
        btrfsbackup-backup-model
        btrfsbackup-config-domain
        btrfsbackup-core
        btrfsbackup-transfer
        PkgConfig::BLKID
        PkgConfig::BTRFSUTIL
        PkgConfig::MOUNT
        PkgConfig::UDEV
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-platform-linux PRIVATE -Wall -Wextra -Wpedantic)
endif()
