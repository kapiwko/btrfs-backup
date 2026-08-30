# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(btrfsbackup-btrfs-operations-tests BtrfsOperationsTests.cpp btrfs-operations-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-command-runner-tests CommandRunnerTests.cpp command-runner-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-device-info-tests DeviceInfoTests.cpp device-info-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-file-lock-tests FileLockTests.cpp file-lock-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-filesystem-tests FilesystemTests.cpp filesystem-tests btrfsbackup-platform-linux)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_btrfsbackup_test(btrfsbackup-file-io-tests FileIoTests.cpp file-io-tests btrfsbackup-platform-linux)
    target_link_options(btrfsbackup-file-io-tests PRIVATE
        -Wl,--wrap=close
        -Wl,--wrap=fchmod
        -Wl,--wrap=fsync
        -Wl,--wrap=mkstemp
        -Wl,--wrap=rename
        -Wl,--wrap=write
    )
endif()

add_btrfsbackup_test(btrfsbackup-mount-info-tests MountInfoTests.cpp mount-info-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-process-tests ProcessTests.cpp process-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-safe-directory-root-tests SafeDirectoryRootTests.cpp safe-directory-root-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-space-check-tests SpaceCheckTests.cpp space-check-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-systemd-target-manager-tests SystemdTargetManagerTests.cpp systemd-target-manager-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-systemd-unit-tests SystemdUnitTests.cpp systemd-unit-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-trusted-directory-tests TrustedDirectoryTests.cpp trusted-directory-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-trusted-executable-tests TrustedExecutableTests.cpp trusted-executable-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-trusted-file-tests TrustedFileTests.cpp trusted-file-tests btrfsbackup-platform-linux)
