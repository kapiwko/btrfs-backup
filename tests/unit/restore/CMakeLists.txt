# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(
    btrfsbackup-restore-catalog-tests
    RepositoryCatalogTests.cpp
    restore-catalog-tests
    btrfsbackup-restore
)
