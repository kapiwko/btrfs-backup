# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(
    btrfsbackup-manager-service-tests
    manager_service_tests.cpp
    manager-service-tests
    btrfsbackup-daemon-core
    btrfsbackup-config-json
)
