# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(variable IN ITEMS CMAKE_COMMAND PYTHON SOURCE_DIR TEST_ROOT RELEASE_BUILD_DIR)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()

set(root "${TEST_ROOT}/release-package")
file(REMOVE_RECURSE "${root}")

function(build_install_tarball destination)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env SOURCE_DATE_EPOCH=1700000000 BUILD_JOBS=2
            "${PYTHON}" "${SOURCE_DIR}/tools/release.py"
            --target tar-install --dist-dir "${destination}"
            --build-dir "${RELEASE_BUILD_DIR}" --skip-tests
        RESULT_VARIABLE result
        ERROR_VARIABLE error
        OUTPUT_QUIET
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "CPack release failed: ${error}")
    endif()
endfunction()

build_install_tarball("${root}/first")
build_install_tarball("${root}/second")
set(archive_name "btrfs-backup-4.0.0-install.tar.gz")
file(SHA256 "${root}/first/${archive_name}" first_hash)
file(SHA256 "${root}/second/${archive_name}" second_hash)
if(NOT first_hash STREQUAL second_hash)
    message(FATAL_ERROR "CPack install archive is not reproducible")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${root}/first/${archive_name}"
    OUTPUT_VARIABLE entries
    RESULT_VARIABLE list_result
)
if(NOT list_result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect CPack install archive")
endif()
foreach(expected IN ITEMS
        "etc/btrfs-backup/hooks.d/"
        "usr/bin/btrfs-backup"
        "usr/bin/btrfs-backupctl"
        "usr/bin/btrfs-backupd"
        "usr/bin/btrfs-backup-device-preparation"
        "usr/lib/systemd/system/btrfs-backup@.service")
    string(FIND "${entries}" "${expected}\n" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Install archive is missing ${expected}")
    endif()
endforeach()
