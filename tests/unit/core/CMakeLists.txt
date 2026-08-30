# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(btrfsbackup-error-code-tests ErrorCodeTests.cpp error-code-tests btrfsbackup-core)
add_btrfsbackup_test(btrfsbackup-error-hierarchy-tests ErrorHierarchyTests.cpp error-hierarchy-tests btrfsbackup-core)
add_btrfsbackup_test(btrfsbackup-identifier-tests IdentifierTests.cpp identifier-tests btrfsbackup-core)
add_btrfsbackup_test(btrfsbackup-runtime-time-tests RuntimeTimeTests.cpp runtime-time-tests btrfsbackup-core)
