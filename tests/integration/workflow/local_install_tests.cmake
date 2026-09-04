# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(variable IN ITEMS CMAKE_COMMAND SOURCE_DIR TEST_ROOT)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()

set(test_dir "${TEST_ROOT}/local-install")
set(dist_dir "${test_dir}/dist")
set(manifest "${test_dir}/commands.txt")
file(REMOVE_RECURSE "${test_dir}")
file(MAKE_DIRECTORY "${dist_dir}")
file(TOUCH
    "${dist_dir}/btrfs-backup-4.0.0-1-x86_64.pkg.tar.zst"
    "${dist_dir}/btrfs-backup-kde-4.0.0-1-x86_64.pkg.tar.zst"
)

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        -DSOURCE_DIR=${SOURCE_DIR}
        -DDIST_DIR=${dist_dir}
        -DLOCAL_INSTALL_SKIP_BUILD=ON
        -DLOCAL_INSTALL_DRY_RUN=ON
        -DLOCAL_INSTALL_MANIFEST=${manifest}
        -P ${SOURCE_DIR}/cmake/InstallLocalRelease.cmake
    RESULT_VARIABLE result
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Local-install dry run failed: ${error}")
endif()

file(READ "${manifest}" commands)
foreach(expected IN ITEMS
        "sudo pacman -U --noconfirm"
        "sudo systemctl restart btrfs-backupd.service"
        "systemctl --user restart btrfs-backup-kde-monitor.service"
        "systemctl --user restart plasma-plasmashell.service")
    string(FIND "${commands}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing '${expected}' in local-install command manifest")
    endif()
endforeach()

file(TOUCH "${dist_dir}/btrfs-backup-4.0.1-1-x86_64.pkg.tar.zst")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        -DSOURCE_DIR=${SOURCE_DIR}
        -DDIST_DIR=${dist_dir}
        -DLOCAL_INSTALL_SKIP_BUILD=ON
        -DLOCAL_INSTALL_DRY_RUN=ON
        -DLOCAL_INSTALL_MANIFEST=${manifest}
        -P ${SOURCE_DIR}/cmake/InstallLocalRelease.cmake
    RESULT_VARIABLE duplicate_result
    OUTPUT_QUIET
    ERROR_QUIET
)
if(duplicate_result EQUAL 0)
    message(FATAL_ERROR "Local install accepted multiple base packages")
endif()

file(REMOVE "${dist_dir}/btrfs-backup-4.0.1-1-x86_64.pkg.tar.zst")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        -DSOURCE_DIR=${SOURCE_DIR}
        -DDIST_DIR=${dist_dir}
        -DLOCAL_INSTALL_BUILD_OPTIONS=--full-tests
        -DLOCAL_INSTALL_SKIP_BUILD=ON
        -DLOCAL_INSTALL_DRY_RUN=ON
        -DLOCAL_INSTALL_MANIFEST=${manifest}
        -P ${SOURCE_DIR}/cmake/InstallLocalRelease.cmake
    RESULT_VARIABLE unsafe_option_result
    OUTPUT_QUIET
    ERROR_QUIET
)
if(unsafe_option_result EQUAL 0)
    message(FATAL_ERROR "Local install accepted the privileged full-test option")
endif()
