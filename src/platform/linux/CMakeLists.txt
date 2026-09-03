# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-platform-linux-base STATIC
    OwnedFileDescriptor.cpp
)
target_restricted_include_directories(btrfsbackup-platform-linux-base
    PUBLIC_HEADERS
        platform/linux/OwnedFileDescriptor.hpp
)

add_library(btrfsbackup-platform-linux-process STATIC
    process/ChildProcess.cpp
    process/CommandCancellationSignal.cpp
    process/ControlledCommandSession.cpp
    process/PosixCommandRunner.cpp
    process/Process.cpp
    process/ProcessEnvironment.cpp
    process/ProcessSpawn.cpp
    process/TerminationSignalMonitor.cpp
)
target_restricted_include_directories(btrfsbackup-platform-linux-process
    PUBLIC_HEADERS
        platform/linux/process/ChildProcess.hpp
        platform/linux/process/PosixCommandRunner.hpp
        platform/linux/process/Process.hpp
        platform/linux/process/ProcessEnvironment.hpp
        platform/linux/process/ProcessSpawn.hpp
        platform/linux/process/TerminationSignalMonitor.hpp
    PRIVATE_HEADERS
        platform/linux/process/CommandCancellationSignal.hpp
        platform/linux/process/ControlledCommandSession.hpp
)
target_link_libraries(btrfsbackup-platform-linux-process
    PUBLIC
        btrfsbackup-backup-ports
    PRIVATE
        btrfsbackup-core
        btrfsbackup-platform-linux-base
)

add_library(btrfsbackup-platform-linux-filesystem STATIC
    filesystem/FileBackupRunLeaseProvider.cpp
    filesystem/FileIo.cpp
    filesystem/FileLock.cpp
    filesystem/InotifyFileChangeWatcher.cpp
    filesystem/PosixDurableFileOperations.cpp
    filesystem/PosixFileSystem.cpp
    filesystem/SafeDirectoryRoot.cpp
    filesystem/SecretFile.cpp
    filesystem/TrustedDirectory.cpp
    filesystem/TrustedExecutable.cpp
    filesystem/TrustedFile.cpp
)
target_restricted_include_directories(btrfsbackup-platform-linux-filesystem
    PUBLIC_HEADERS
        platform/linux/filesystem/FileBackupRunLeaseProvider.hpp
        platform/linux/filesystem/FileIo.hpp
        platform/linux/filesystem/FileLock.hpp
        platform/linux/filesystem/InotifyFileChangeWatcher.hpp
        platform/linux/filesystem/PosixDurableFileOperations.hpp
        platform/linux/filesystem/PosixFileSystem.hpp
        platform/linux/filesystem/SafeDirectoryRoot.hpp
        platform/linux/filesystem/SecretFile.hpp
        platform/linux/filesystem/TrustedDirectory.hpp
        platform/linux/filesystem/TrustedExecutable.hpp
        platform/linux/filesystem/TrustedFile.hpp
)
target_link_libraries(btrfsbackup-platform-linux-filesystem
    PUBLIC
        btrfsbackup-backup-ports
        btrfsbackup-platform-linux-base
        btrfsbackup-state-persistence-ports
    PRIVATE
        btrfsbackup-config-domain
        btrfsbackup-core
        btrfsbackup-transfer
)

add_library(btrfsbackup-platform-linux-storage STATIC
    storage/BlockDeviceMetadata.cpp
    storage/CryptsetupOperations.cpp
    storage/DeviceInfo.cpp
    storage/FilesystemSpaceProbe.cpp
    storage/LibBtrfsOperations.cpp
    storage/MountInfo.cpp
    storage/SignatureOperations.cpp
    storage/SpaceCheck.cpp
)

if(BUILD_SYSTEM_MANAGER)
    add_library(btrfsbackup-platform-linux-provisioning STATIC
        storage/provisioning/BtrfsFilesystemFormatter.cpp
        storage/provisioning/ExistingTargetMountOperations.cpp
        storage/provisioning/PartitionTableOperations.cpp
        storage/provisioning/SystemStorageTopologyReader.cpp
    )
    target_restricted_include_directories(btrfsbackup-platform-linux-provisioning
        PUBLIC_HEADERS
            platform/linux/storage/provisioning/BtrfsFilesystemFormatter.hpp
            platform/linux/storage/provisioning/ExistingTargetMountOperations.hpp
            platform/linux/storage/provisioning/PartitionTableOperations.hpp
            platform/linux/storage/provisioning/SystemStorageTopologyReader.hpp
    )
    target_link_libraries(btrfsbackup-platform-linux-provisioning
        PUBLIC
            btrfsbackup-provisioning
        PRIVATE
            btrfsbackup-backup-ports
            btrfsbackup-core
            btrfsbackup-platform-linux-base
            PkgConfig::BLKID
            PkgConfig::FDISK
            PkgConfig::MOUNT
            PkgConfig::UDEV
    )
endif()

add_library(btrfsbackup-platform-linux-restore STATIC
    restore/PosixRestoreOperations.cpp
)
target_restricted_include_directories(btrfsbackup-platform-linux-restore
    PUBLIC_HEADERS
        platform/linux/restore/PosixRestoreOperations.hpp
)
target_link_libraries(btrfsbackup-platform-linux-restore
    PUBLIC
        btrfsbackup-restore
    PRIVATE
        btrfsbackup-core
        PkgConfig::BTRFSUTIL
)
target_restricted_include_directories(btrfsbackup-platform-linux-storage
    PUBLIC_HEADERS
        platform/linux/storage/BlockDeviceMetadata.hpp
        platform/linux/storage/DeviceInfo.hpp
        platform/linux/storage/CryptsetupOperations.hpp
        platform/linux/storage/FilesystemSpaceProbe.hpp
        platform/linux/storage/LibBtrfsOperations.hpp
        platform/linux/storage/MountInfo.hpp
        platform/linux/storage/SignatureOperations.hpp
        platform/linux/storage/SpaceCheck.hpp
)
target_link_libraries(btrfsbackup-platform-linux-storage
    PUBLIC
        btrfsbackup-backup-ports
    PRIVATE
        btrfsbackup-backup-model
        btrfsbackup-config-json
        btrfsbackup-core
        btrfsbackup-platform-linux-base
        PkgConfig::BLKID
        PkgConfig::BTRFSUTIL
        PkgConfig::CRYPTSETUP
        PkgConfig::MOUNT
)

add_library(btrfsbackup-platform-linux-transfer STATIC
    transfer/BoundedDiagnosticBuffer.cpp
    transfer/PosixCancellationSignal.cpp
    transfer/PosixTransferPipeline.cpp
    transfer/PosixTransferProcess.cpp
    transfer/PosixTransferPump.cpp
    transfer/PosixTransferRuntime.cpp
    transfer/PosixTransferSession.cpp
    transfer/Splice.cpp
    transfer/ThreadSigpipeBlock.cpp
    transfer/TransferChildTermination.cpp
    transfer/TransferProgressReporter.cpp
)
target_restricted_include_directories(btrfsbackup-platform-linux-transfer
    PUBLIC_HEADERS
        platform/linux/transfer/PosixCancellationSignal.hpp
        platform/linux/transfer/PosixTransferPipeline.hpp
        platform/linux/transfer/PosixTransferProcess.hpp
        platform/linux/transfer/PosixTransferPump.hpp
        platform/linux/transfer/Splice.hpp
        platform/linux/transfer/ThreadSigpipeBlock.hpp
    PRIVATE_HEADERS
        platform/linux/transfer/BoundedDiagnosticBuffer.hpp
        platform/linux/transfer/PosixTransferRuntime.hpp
        platform/linux/transfer/PosixTransferSession.hpp
        platform/linux/transfer/TransferChildTermination.hpp
        platform/linux/transfer/TransferProgressReporter.hpp
)
target_link_libraries(btrfsbackup-platform-linux-transfer
    PUBLIC
        btrfsbackup-platform-linux-base
        btrfsbackup-platform-linux-process
    PRIVATE
        btrfsbackup-core
        btrfsbackup-platform-linux-filesystem
        btrfsbackup-transfer
)

add_library(btrfsbackup-platform-linux-systemd STATIC
    systemd/LinuxSystemConfigurationActivator.cpp
    systemd/SystemdMountedTargetSession.cpp
    systemd/SystemdTargetManager.cpp
    systemd/SystemdUnit.cpp
)
target_restricted_include_directories(btrfsbackup-platform-linux-systemd
    PUBLIC_HEADERS
        platform/linux/systemd/LinuxSystemConfigurationActivator.hpp
        platform/linux/systemd/SystemdTargetManager.hpp
        platform/linux/systemd/SystemdUnit.hpp
    PRIVATE_HEADERS
        platform/linux/systemd/SystemdMountedTargetSession.hpp
)
target_link_libraries(btrfsbackup-platform-linux-systemd
    PUBLIC
        btrfsbackup-backup-ports
        btrfsbackup-config-ports
    PRIVATE
        btrfsbackup-core
        btrfsbackup-platform-linux-process
)

add_library(btrfsbackup-platform-linux INTERFACE)
target_compile_features(btrfsbackup-platform-linux INTERFACE cxx_std_23)
target_link_libraries(btrfsbackup-platform-linux INTERFACE
    btrfsbackup-platform-linux-base
    btrfsbackup-platform-linux-filesystem
    btrfsbackup-platform-linux-process
    btrfsbackup-platform-linux-restore
    btrfsbackup-platform-linux-storage
    btrfsbackup-platform-linux-systemd
    btrfsbackup-platform-linux-transfer
)
if(BUILD_SYSTEM_MANAGER)
    target_link_libraries(btrfsbackup-platform-linux INTERFACE
        btrfsbackup-platform-linux-provisioning
    )
endif()

set(linux_platform_targets
    btrfsbackup-platform-linux-base
    btrfsbackup-platform-linux-filesystem
    btrfsbackup-platform-linux-process
    btrfsbackup-platform-linux-restore
    btrfsbackup-platform-linux-storage
    btrfsbackup-platform-linux-systemd
    btrfsbackup-platform-linux-transfer
)
if(BUILD_SYSTEM_MANAGER)
    list(APPEND linux_platform_targets btrfsbackup-platform-linux-provisioning)
endif()
foreach(target IN LISTS linux_platform_targets)
    target_compile_features(${target} PUBLIC cxx_std_23)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
