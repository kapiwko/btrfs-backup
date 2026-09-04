# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(legacy IN ITEMS
        tests/integration/docker/real-btrfs-test.sh
        tests/integration/docker/run-real-btrfs.sh)
    if(EXISTS "${SOURCE_DIR}/${legacy}")
        message(FATAL_ERROR "Legacy real-Btrfs shell harness returned: ${legacy}")
    endif()
endforeach()

foreach(replacement IN ITEMS
        tests/integration/docker/run_real_btrfs.py
        tests/integration/docker/real_btrfs_suite.py)
    if(NOT EXISTS "${SOURCE_DIR}/${replacement}")
        message(FATAL_ERROR "Missing real-Btrfs Python harness: ${replacement}")
    endif()
endforeach()
