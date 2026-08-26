# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(btrfsbackup-backup-run-serialization-tests backup_run_serialization_tests.cpp backup-run-serialization-tests btrfsbackup-state)
add_btrfsbackup_test(btrfsbackup-run-checkpoint-store-tests run_checkpoint_store_tests.cpp run-checkpoint-store-tests btrfsbackup-state)
add_btrfsbackup_test(btrfsbackup-run-status-projection-tests run_status_projection_tests.cpp run-status-projection-tests btrfsbackup-state)
add_btrfsbackup_test(btrfsbackup-run-state-tests run_state_tests.cpp run-state-tests btrfsbackup-state)
add_btrfsbackup_test(btrfsbackup-run-status-tests run_status_tests.cpp run-status-tests btrfsbackup-state-model)
add_btrfsbackup_test(btrfsbackup-status-writer-tests status_writer_tests.cpp status-writer-tests btrfsbackup-state)
