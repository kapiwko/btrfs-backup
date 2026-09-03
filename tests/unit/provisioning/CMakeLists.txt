# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(
    btrfsbackup-configured-backup-target-marker-tests
    ConfiguredBackupTargetMarkerTests.cpp
    configured-backup-target-marker-tests
    btrfsbackup-provisioning
    btrfsbackup-core
)

add_btrfsbackup_test(
    btrfsbackup-device-preparation-plan-builder-tests
    DevicePreparationPlanBuilderTests.cpp
    device-preparation-plan-builder-tests
    btrfsbackup-provisioning
    btrfsbackup-core
)

add_btrfsbackup_test(
    btrfsbackup-storage-safety-inspector-tests
    StorageSafetyInspectorTests.cpp
    storage-safety-inspector-tests
    btrfsbackup-provisioning
    btrfsbackup-core
)
