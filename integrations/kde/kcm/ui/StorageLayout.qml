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

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: Kirigami.Units.gridUnit * 3
        spacing: 1

        Repeater {
            model: root.regions || []
            delegate: Rectangle {
                id: segment
                required property var modelData
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.preferredWidth: Math.max(1, Number(segment.modelData.sectorCount || 0) / root.totalSectors * 1000)
                Layout.minimumWidth: 2
                color: segment.modelData.kind === "unallocated" ? Kirigami.Theme.alternateBackgroundColor
                    : segment.modelData.dataWillBeErased ? Kirigami.Theme.negativeTextColor
                    : segment.modelData.kind === "backup-partition" ? Kirigami.Theme.positiveTextColor
                    : Kirigami.Theme.highlightColor
                border.width: segment.modelData.candidateId === root.selectedRegionId ? 2 : 1
                border.color: Kirigami.Theme.textColor

                QQC2.Label {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    clip: true
                    text: segment.modelData.partitionLabel || segment.modelData.filesystemLabel
                        || segment.modelData.filesystemType
                        || (segment.modelData.kind === "unallocated"
                            ? translations.i18n("Free") : translations.i18n("Backup"))
                    color: segment.modelData.kind === "unallocated"
                        ? Kirigami.Theme.textColor : Kirigami.Theme.highlightedTextColor
                }
            }
        }
    }

    Repeater {
        model: root.regions || []
        delegate: QQC2.Label {
            required property var modelData
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: {
                const name = modelData.path || modelData.partitionLabel || modelData.filesystemLabel
                    || (modelData.kind === "unallocated"
                        ? translations.i18n("Free space") : translations.i18n("Backup partition"))
                const size = root.formatBytes(Number(modelData.sectorCount || 0) * root.logicalSectorSize)
                const outcome = modelData.dataWillBeErased ? translations.i18n("data will be erased")
                    : modelData.changed ? translations.i18n("will become LUKS2 with Btrfs")
                        : translations.i18n("unchanged")
                return name + " - " + size + " - " + outcome
            }
        }
    }

    function formatBytes(value) {
        const gib = value / 1073741824
        if (gib >= 1)
            return translations.i18n("%1 GiB", Math.round(gib * 10) / 10)
        return translations.i18n("%1 MiB", Math.round(value / 1048576))
    }
}
