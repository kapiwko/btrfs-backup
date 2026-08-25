# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(btrfsbackup-backup-run-persistence-tests backup_run_persistence_tests.cpp backup-run-persistence-tests btrfsbackup-state)
add_btrfsbackup_test(btrfsbackup-run-state-tests run_state_tests.cpp run-state-tests btrfsbackup-state)
add_btrfsbackup_test(btrfsbackup-run-status-tests run_status_tests.cpp run-status-tests btrfsbackup-state-model)
add_btrfsbackup_test(btrfsbackup-status-writer-tests status_writer_tests.cpp status-writer-tests btrfsbackup-state)
