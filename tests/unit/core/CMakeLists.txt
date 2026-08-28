# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(btrfsbackup-error-code-tests error_code_tests.cpp error-code-tests btrfsbackup-core)
add_btrfsbackup_test(btrfsbackup-error-hierarchy-tests error_hierarchy_tests.cpp error-hierarchy-tests btrfsbackup-core)
add_btrfsbackup_test(btrfsbackup-identifier-tests identifier_tests.cpp identifier-tests btrfsbackup-core)
add_btrfsbackup_test(btrfsbackup-runtime-time-tests runtime_time_tests.cpp runtime-time-tests btrfsbackup-core)
