add_btrfsbackup_test(btrfsbackup-btrfs-operations-tests btrfs_operations_tests.cpp btrfs-operations-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-command-runner-tests command_runner_tests.cpp command-runner-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-device-info-tests device_info_tests.cpp device-info-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-file-lock-tests file_lock_tests.cpp file-lock-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-filesystem-tests filesystem_tests.cpp filesystem-tests btrfsbackup-platform-linux)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_btrfsbackup_test(btrfsbackup-file-io-tests file_io_tests.cpp file-io-tests btrfsbackup-platform-linux)
    target_link_options(btrfsbackup-file-io-tests PRIVATE
        -Wl,--wrap=close
        -Wl,--wrap=fchmod
        -Wl,--wrap=fsync
        -Wl,--wrap=mkstemp
        -Wl,--wrap=rename
        -Wl,--wrap=write
    )
endif()

add_btrfsbackup_test(btrfsbackup-mount-info-tests mount_info_tests.cpp mount-info-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-process-tests process_tests.cpp process-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-safe-directory-root-tests safe_directory_root_tests.cpp safe-directory-root-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-space-check-tests space_check_tests.cpp space-check-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-trusted-executable-tests trusted_executable_tests.cpp trusted-executable-tests btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-trusted-file-tests trusted_file_tests.cpp trusted-file-tests btrfsbackup-platform-linux)
