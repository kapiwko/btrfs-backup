# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(btrfsbackup-retention-plan-tests retention_plan_tests.cpp retention-plan-tests btrfsbackup-backup-model)
add_btrfsbackup_test(btrfsbackup-snapshot-inventory-tests snapshot_inventory_tests.cpp snapshot-inventory-tests btrfsbackup-backup-model)
add_btrfsbackup_test(btrfsbackup-backup-run-action-handler-tests backup_run_action_handler_tests.cpp backup-run-action-handler-tests btrfsbackup-backup btrfsbackup-state btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-backup-run-checkpoint-policy-tests backup_run_checkpoint_policy_tests.cpp backup-run-checkpoint-policy-tests btrfsbackup-backup)
add_btrfsbackup_test(btrfsbackup-backup-run-tests backup_run_tests.cpp backup-run-tests btrfsbackup-backup)
add_btrfsbackup_test(btrfsbackup-backup-planning-snapshot-tests backup_planning_snapshot_tests.cpp backup-planning-snapshot-tests btrfsbackup-backup-model)
add_btrfsbackup_test(btrfsbackup-backup-plan-builder-tests backup_plan_builder_tests.cpp backup-plan-builder-tests btrfsbackup-backup)
add_btrfsbackup_test(btrfsbackup-backup-service-tests backup_service_tests.cpp backup-service-tests btrfsbackup-backup)
add_btrfsbackup_test(btrfsbackup-backup-run-executor-tests backup_run_executor_tests.cpp backup-run-executor-tests btrfsbackup-backup)
add_btrfsbackup_test(btrfsbackup-backup-run-plan-tests backup_run_plan_tests.cpp backup-run-plan-tests btrfsbackup-backup-model)
add_btrfsbackup_test(btrfsbackup-incremental-parent-tests incremental_parent_tests.cpp incremental-parent-tests btrfsbackup-backup-model)
add_btrfsbackup_test(btrfsbackup-pending-recovery-plan-tests pending_recovery_plan_tests.cpp pending-recovery-plan-tests btrfsbackup-backup-model btrfsbackup-state btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-snapshot-transfer-tests snapshot_transfer_tests.cpp snapshot-transfer-tests btrfsbackup-backup)
add_btrfsbackup_test(btrfsbackup-target-mount-validation-tests target_mount_validation_tests.cpp target-mount-validation-tests btrfsbackup-backup-domain)
add_btrfsbackup_test(
    btrfsbackup-transfer-pipeline-tests
    transfer_pipeline_tests.cpp
    transfer-pipeline-tests
    btrfsbackup-transfer
    btrfsbackup-platform-linux
)
