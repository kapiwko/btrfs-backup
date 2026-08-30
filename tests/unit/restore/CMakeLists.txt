# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(
    btrfsbackup-restore-catalog-tests
    RepositoryCatalogTests.cpp
    restore-catalog-tests
    btrfsbackup-restore
)
add_btrfsbackup_test(
    btrfsbackup-restore-safety-tests
    RestoreSafetyTests.cpp
    restore-safety-tests
    btrfsbackup-restore
    btrfsbackup-platform-linux-restore
)
add_btrfsbackup_test(
    btrfsbackup-restore-engine-tests
    RestoreEngineTests.cpp
    restore-engine-tests
    btrfsbackup-restore
    btrfsbackup-platform-linux-restore
)
