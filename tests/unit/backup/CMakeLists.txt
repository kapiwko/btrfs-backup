# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(btrfsbackup-retention-plan-tests RetentionPlanTests.cpp retention-plan-tests btrfsbackup-backup-model)
add_btrfsbackup_test(btrfsbackup-snapshot-inventory-tests SnapshotInventoryTests.cpp snapshot-inventory-tests btrfsbackup-backup-model)
add_btrfsbackup_test(btrfsbackup-backup-run-action-handler-tests execution/actions/BackupRunActionHandlerTests.cpp backup-run-action-handler-tests btrfsbackup-backup btrfsbackup-state btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-backup-run-checkpoint-policy-tests execution/BackupRunCheckpointPolicyTests.cpp backup-run-checkpoint-policy-tests btrfsbackup-backup)
add_btrfsbackup_test(btrfsbackup-backup-run-tests execution/BackupRunTests.cpp backup-run-tests btrfsbackup-backup btrfsbackup-backup-testing)
add_btrfsbackup_test(btrfsbackup-backup-planning-snapshot-tests BackupPlanningSnapshotTests.cpp backup-planning-snapshot-tests btrfsbackup-backup-model)
add_btrfsbackup_test(btrfsbackup-backup-plan-builder-tests planning/BackupPlanBuilderTests.cpp backup-plan-builder-tests btrfsbackup-backup)
add_btrfsbackup_test(btrfsbackup-backup-service-tests BackupServiceTests.cpp backup-service-tests btrfsbackup-backup)
add_btrfsbackup_test(btrfsbackup-default-backup-run-factory-tests execution/DefaultBackupRunFactoryTests.cpp default-backup-run-factory-tests btrfsbackup-backup btrfsbackup-backup-testing)
add_btrfsbackup_test(btrfsbackup-backup-run-executor-tests execution/BackupRunExecutorTests.cpp backup-run-executor-tests btrfsbackup-backup)
add_btrfsbackup_test(btrfsbackup-backup-run-plan-tests BackupRunPlanTests.cpp backup-run-plan-tests btrfsbackup-backup-model)
add_btrfsbackup_test(btrfsbackup-incremental-parent-tests IncrementalParentTests.cpp incremental-parent-tests btrfsbackup-backup-model)
add_btrfsbackup_test(btrfsbackup-pending-recovery-plan-tests PendingRecoveryPlanTests.cpp pending-recovery-plan-tests btrfsbackup-backup-model btrfsbackup-state btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-snapshot-transfer-tests SnapshotTransferTests.cpp snapshot-transfer-tests btrfsbackup-backup)
add_btrfsbackup_test(btrfsbackup-backup-preflight-tests planning/BackupPreflightTests.cpp backup-preflight-tests btrfsbackup-backup)
add_btrfsbackup_test(btrfsbackup-backup-preflight-validation-tests planning/BackupPreflightValidationTests.cpp backup-preflight-validation-tests btrfsbackup-backup)
