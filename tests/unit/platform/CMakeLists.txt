# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(btrfsbackup-btrfs-operations-tests linux/storage/BtrfsOperationsTests.cpp btrfs-operations-tests btrfsbackup-platform-linux-storage)
add_btrfsbackup_test(btrfsbackup-command-runner-tests linux/process/CommandRunnerTests.cpp command-runner-tests btrfsbackup-platform-linux-process)
add_btrfsbackup_test(btrfsbackup-device-info-tests linux/storage/DeviceInfoTests.cpp device-info-tests btrfsbackup-platform-linux-storage)
add_btrfsbackup_test(btrfsbackup-file-lock-tests linux/filesystem/FileLockTests.cpp file-lock-tests btrfsbackup-platform-linux-filesystem)
add_btrfsbackup_test(btrfsbackup-filesystem-tests linux/filesystem/FilesystemTests.cpp filesystem-tests btrfsbackup-platform-linux-filesystem)
add_btrfsbackup_test(btrfsbackup-secret-file-tests linux/filesystem/SecretFileTests.cpp secret-file-tests btrfsbackup-platform-linux-filesystem)
add_btrfsbackup_test(btrfsbackup-signature-operations-tests linux/storage/SignatureOperationsTests.cpp signature-operations-tests btrfsbackup-platform-linux-storage)
add_btrfsbackup_test(btrfsbackup-inotify-file-change-watcher-tests linux/filesystem/InotifyFileChangeWatcherTests.cpp inotify-file-change-watcher-tests btrfsbackup-platform-linux-filesystem)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_btrfsbackup_test(btrfsbackup-file-io-tests linux/filesystem/FileIoTests.cpp file-io-tests btrfsbackup-platform-linux-filesystem)
    target_link_options(btrfsbackup-file-io-tests PRIVATE
        -Wl,--wrap=close
        -Wl,--wrap=fchmod
        -Wl,--wrap=fsync
        -Wl,--wrap=mkstemp
        -Wl,--wrap=rename
        -Wl,--wrap=write
    )
endif()

add_btrfsbackup_test(btrfsbackup-mount-info-tests linux/storage/MountInfoTests.cpp mount-info-tests btrfsbackup-platform-linux-storage)
add_btrfsbackup_test(btrfsbackup-owned-file-descriptor-tests OwnedFileDescriptorTests.cpp owned-file-descriptor-tests btrfsbackup-core btrfsbackup-platform-linux-base)
add_btrfsbackup_test(btrfsbackup-process-tests linux/process/ProcessTests.cpp process-tests btrfsbackup-platform-linux-process)
add_btrfsbackup_test(btrfsbackup-safe-directory-root-tests linux/filesystem/SafeDirectoryRootTests.cpp safe-directory-root-tests btrfsbackup-platform-linux-filesystem)
add_btrfsbackup_test(btrfsbackup-space-check-tests linux/storage/SpaceCheckTests.cpp space-check-tests btrfsbackup-platform-linux-storage)
add_btrfsbackup_test(btrfsbackup-systemd-target-manager-tests linux/systemd/SystemdTargetManagerTests.cpp systemd-target-manager-tests btrfsbackup-platform-linux-systemd)
add_btrfsbackup_test(btrfsbackup-systemd-unit-tests linux/systemd/SystemdUnitTests.cpp systemd-unit-tests btrfsbackup-platform-linux-systemd)
add_btrfsbackup_test(btrfsbackup-trusted-directory-tests linux/filesystem/TrustedDirectoryTests.cpp trusted-directory-tests btrfsbackup-platform-linux-filesystem)
add_btrfsbackup_test(btrfsbackup-trusted-executable-tests linux/filesystem/TrustedExecutableTests.cpp trusted-executable-tests btrfsbackup-platform-linux-filesystem btrfsbackup-platform-linux-process)
add_btrfsbackup_test(btrfsbackup-trusted-file-tests linux/filesystem/TrustedFileTests.cpp trusted-file-tests btrfsbackup-platform-linux-filesystem)
add_btrfsbackup_test(btrfsbackup-thread-sigpipe-block-tests linux/transfer/ThreadSigpipeBlockTests.cpp thread-sigpipe-block-tests btrfsbackup-platform-linux-transfer)
target_include_directories(btrfsbackup-thread-sigpipe-block-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_btrfsbackup_test(
    btrfsbackup-transfer-pipeline-tests
    linux/transfer/TransferPipelineTests.cpp
    transfer-pipeline-tests
    btrfsbackup-transfer
    btrfsbackup-platform-linux-transfer
)
