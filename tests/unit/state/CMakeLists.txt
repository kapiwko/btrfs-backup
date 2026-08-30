# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(btrfsbackup-backup-run-serialization-tests BackupRunSerializationTests.cpp backup-run-serialization-tests btrfsbackup-state)
add_btrfsbackup_test(btrfsbackup-cancellation-watch-lifecycle-tests CancellationWatchLifecycleTests.cpp cancellation-watch-lifecycle-tests btrfsbackup-state)
add_btrfsbackup_test(btrfsbackup-persistent-document-operations-tests PersistentDocumentOperationsTests.cpp persistent-document-operations-tests btrfsbackup-state)
add_btrfsbackup_test(btrfsbackup-run-checkpoint-store-tests RunCheckpointStoreTests.cpp run-checkpoint-store-tests btrfsbackup-state btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-run-status-projection-tests RunStatusProjectionTests.cpp run-status-projection-tests btrfsbackup-state btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-run-state-tests RunStateTests.cpp run-state-tests btrfsbackup-state btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-run-status-tests RunStatusTests.cpp run-status-tests btrfsbackup-state-model)
add_btrfsbackup_test(btrfsbackup-run-status-document-codec-tests RunStatusDocumentCodecTests.cpp run-status-document-codec-tests btrfsbackup-status-documents btrfsbackup-config-json)
add_btrfsbackup_test(btrfsbackup-status-writer-tests StatusWriterTests.cpp status-writer-tests btrfsbackup-state btrfsbackup-platform-linux)
