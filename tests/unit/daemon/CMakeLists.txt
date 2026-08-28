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

add_btrfsbackup_test(
    btrfsbackup-manager-json-codec-tests
    manager_json_codec_tests.cpp
    manager-json-codec-tests
    btrfsbackup-daemon-core
    btrfsbackup-config-json
)

add_btrfsbackup_test(
    btrfsbackup-manager-error-mapper-tests
    manager_error_mapper_tests.cpp
    manager-error-mapper-tests
    btrfsbackup-daemon-core
    btrfsbackup-core
)

add_btrfsbackup_test(
    btrfsbackup-operational-control-service-tests
    operational_control_service_tests.cpp
    operational-control-service-tests
    btrfsbackup-daemon-core
    btrfsbackup-core
)
