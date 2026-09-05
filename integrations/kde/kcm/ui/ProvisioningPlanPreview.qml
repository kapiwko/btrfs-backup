// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    required property var workflow
    required property var translations

    Layout.fillWidth: true
    visible: root.workflow.provisioning.plan.planId !== undefined
    spacing: Kirigami.Units.smallSpacing

    Kirigami.Heading {
        text: root.translations.i18n("Before")
        level: 3
    }
    StorageLayout {
        Layout.fillWidth: true
        regions: root.workflow.provisioning.plan.before?.regions ?? []
        selectedRegionId: root.workflow.provisioning.plan.partitionId
            ?? root.workflow.provisioning.plan.freeRegionId ?? ""
        preview: false
        logicalSectorSize: root.workflow.provisioning.plan.before?.logicalSectorSize ?? 512
        formatBytes: value => root.workflow.provisioning.formatBytes(value)
    }
    Kirigami.Heading {
        text: root.workflow.adoption
            ? root.translations.i18n("After adoption")
            : root.translations.i18n("After preparation")
        level: 3
    }
    StorageLayout {
        Layout.fillWidth: true
        regions: root.workflow.provisioning.plan.after?.regions ?? []
        selectedRegionId: root.workflow.provisioning.plan.partitionId ?? "planned-backup-partition"
        preview: true
        logicalSectorSize: root.workflow.provisioning.plan.after?.logicalSectorSize ?? 512
        formatBytes: value => root.workflow.provisioning.formatBytes(value)
    }
    QQC2.Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: root.workflow.provisioning.plan.mode === "adopt-existing-target"
            ? root.translations.i18n("The existing repository and all data on this partition will remain unchanged.")
            : root.workflow.provisioning.plan.mode === "reformat-existing-partition"
            ? root.translations.i18n("Only data on the selected partition will be removed. Other partitions will remain unchanged.")
            : root.workflow.provisioning.plan.mode === "create-partition-in-unallocated-space"
            ? root.translations.i18n("A new LUKS2 and Btrfs backup partition will be created in the selected free space. Existing partitions will remain unchanged.")
            : root.translations.i18n("All existing partitions on this disk will be removed. The resulting backup target will use LUKS2 encryption and Btrfs.")
    }
}
