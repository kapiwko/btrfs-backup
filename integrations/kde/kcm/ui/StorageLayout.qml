// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var regions
    required property string selectedRegionId
    required property bool preview
    property int logicalSectorSize: 512

    spacing: Kirigami.Units.smallSpacing

    KI18n.KI18nContext { id: translations; translationDomain: "kcm_btrfsbackup" }

    readonly property real totalSectors: {
        let total = 0
        for (const region of root.regions || [])
            total += Number(region.sectorCount || 0)
        return Math.max(1, total)
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.minimumHeight: Kirigami.Units.gridUnit * 3.5
        radius: Kirigami.Units.smallSpacing
        color: Kirigami.Theme.backgroundColor
        border.width: 1
        border.color: Kirigami.Theme.separatorColor

        RowLayout {
            anchors.fill: parent
            anchors.margins: 2
            spacing: 2

            Repeater {
                model: root.regions || []
                delegate: Rectangle {
                    id: segment
                    required property var modelData
                    readonly property string regionName: root.regionName(segment.modelData)
                    readonly property string regionDetails: root.regionDetails(segment.modelData)

                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Layout.preferredWidth: Math.max(1, Number(segment.modelData.sectorCount || 0)
                        / root.totalSectors * 1000)
                    Layout.minimumWidth: 3
                    radius: Math.max(2, Kirigami.Units.smallSpacing - 2)
                    color: root.regionColor(segment.modelData)
                    border.width: 1
                    border.color: Qt.darker(segment.color, 1.2)
                    clip: true

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 4
                        visible: segment.modelData.dataWillBeErased
                        color: Kirigami.Theme.negativeTextColor
                    }

                    Column {
                        anchors.centerIn: parent
                        width: Math.max(0, parent.width - Kirigami.Units.largeSpacing)
                        spacing: 1
                        visible: width >= Kirigami.Units.gridUnit * 2

                        QQC2.Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: segment.regionName
                            font.bold: true
                            color: root.regionTextColor(segment.modelData)
                        }
                        QQC2.Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: segment.regionDetails
                            color: root.regionTextColor(segment.modelData)
                            opacity: 0.9
                        }
                    }

                    Accessible.name: segment.regionName + ", " + segment.regionDetails
                }
            }
        }
    }

    Repeater {
        model: root.regions || []
        delegate: RowLayout {
            id: detailRow
            required property var modelData
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Rectangle {
                Layout.preferredWidth: Kirigami.Units.smallSpacing * 1.5
                Layout.preferredHeight: Layout.preferredWidth
                radius: Layout.preferredWidth / 2
                color: root.regionColor(detailRow.modelData)
                border.width: 1
                border.color: Qt.darker(color, 1.2)
            }
            QQC2.Label {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: root.regionName(detailRow.modelData) + " · "
                    + root.regionDetails(detailRow.modelData)
            }
            QQC2.Label {
                text: detailRow.modelData.dataWillBeErased ? translations.i18n("data will be erased")
                    : detailRow.modelData.changed ? translations.i18n("will become LUKS2 with Btrfs")
                        : translations.i18n("unchanged")
                color: detailRow.modelData.dataWillBeErased
                    ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
            }
        }
    }

    function regionName(region) {
        if (region.kind === "unallocated")
            return translations.i18n("Free space")
        if (region.partitionNumber)
            return translations.i18n("Partition %1", region.partitionNumber)
        return translations.i18n("Backup partition")
    }

    function regionDetails(region) {
        const size = root.formatBytes(Number(region.sectorCount || 0) * root.logicalSectorSize)
        if (region.kind === "backup-partition")
            return size + " · LUKS2 · Btrfs"
        if (region.kind === "unallocated")
            return size
        return region.encrypted ? size + " · LUKS2" : size
    }

    function regionColor(region) {
        if (region.kind === "unallocated")
            return Kirigami.Theme.alternateBackgroundColor
        if (region.kind === "backup-partition")
            return Kirigami.Theme.positiveTextColor
        return Kirigami.Theme.highlightColor
    }

    function regionTextColor(region) {
        return region.kind === "unallocated"
            ? Kirigami.Theme.textColor : Kirigami.Theme.highlightedTextColor
    }

    function formatBytes(value) {
        const gib = value / 1073741824
        if (gib >= 1)
            return translations.i18n("%1 GiB", Math.round(gib * 10) / 10)
        return translations.i18n("%1 MiB", Math.round(value / 1048576))
    }
}
