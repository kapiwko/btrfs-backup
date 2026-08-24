// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import org.btrfsbackup.plasma

pragma ComponentBehavior: Bound

PlasmoidItem {
    id: root

    property bool running: backupStatus.state === "running" || backupStatus.state === "starting" || backupStatus.state === "validating"
    property bool failed: backupStatus.state === "failed"
    property bool estimated: backupStatus.progressAccuracy === "estimated"
    property int progress: backupStatus.overallProgress

    Plasmoid.status: root.running || root.failed ? PlasmaCore.Types.ActiveStatus : PlasmaCore.Types.PassiveStatus
    toolTipMainText: i18n("Btrfs Backups")
    toolTipSubText: backupStatus.lastError || root.statusText(backupStatus.state)

    BackupStatusModel {
        id: backupStatus
        profile: "default"
        Component.onCompleted: start()
    }

    function formatBytes(value) {
        var amount = Number(value || 0)
        var units = ["B", "KiB", "MiB", "GiB", "TiB", "PiB"]
        var index = 0
        while (amount >= 1024 && index < units.length - 1) {
            amount /= 1024
            index++
        }
        return (index === 0 ? amount.toFixed(0) : amount.toFixed(1)) + " " + units[index]
    }

    function formatEta(value) {
        var seconds = Number(value || -1)
        if (seconds < 0)
            return i18n("Unknown")
        var minutes = Math.floor(seconds / 60)
        seconds = Math.floor(seconds % 60)
        if (minutes > 0)
            return i18n("%1 min %2 sec", minutes, seconds)
        return i18n("%1 sec", seconds)
    }

    function statusText(state) {
        switch (state) {
        case "starting":
        case "running":
            return i18n("Backup is in progress")
        case "validated":
            return i18n("Validation completed successfully")
        case "succeeded":
            return i18n("Backup completed successfully")
        case "failed":
            return i18n("Backup failed")
        case "cancelled":
            return i18n("Backup cancelled")
        case "skipped":
            return i18n("Backup skipped")
        default:
            return i18n("No active backup")
        }
    }

    compactRepresentation: MouseArea {
        implicitWidth: Kirigami.Units.iconSizes.smallMedium
        implicitHeight: implicitWidth
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        onClicked: root.expanded = !root.expanded

        Kirigami.Icon {
            anchors.fill: parent
            anchors.margins: Math.max(1, parent.width * 0.13)
            source: root.failed ? "dialog-error" : "drive-harddisk"
            opacity: backupStatus.watcherConnected ? 1 : 0.65
        }

        Canvas {
            id: compactProgress
            anchors.fill: parent
            visible: root.running && root.progress >= 0
            onPaint: {
                var context = getContext("2d")
                context.clearRect(0, 0, width, height)
                context.lineWidth = Math.max(2, width * 0.09)
                context.lineCap = "round"
                context.strokeStyle = Kirigami.Theme.highlightColor
                context.beginPath()
                context.arc(width / 2, height / 2, Math.min(width, height) / 2 - context.lineWidth / 2,
                            -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * Math.min(100, Math.max(0, root.progress)) / 100)
                context.stroke()
            }
            Connections {
                target: backupStatus
                function onStatusChanged() {
                    compactProgress.requestPaint()
                }
            }
        }
    }

    fullRepresentation: Item {
        implicitWidth: Kirigami.Units.gridUnit * 22
        implicitHeight: Kirigami.Units.gridUnit * 18

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            RowLayout {
                Layout.fillWidth: true

                Kirigami.Icon {
                    source: root.failed ? "dialog-error" : "drive-harddisk"
                    implicitWidth: Kirigami.Units.iconSizes.large
                    implicitHeight: implicitWidth
                }

                ColumnLayout {
                    Layout.fillWidth: true

                    Kirigami.Heading {
                        text: i18n("Btrfs Backups")
                        level: 2
                        Layout.fillWidth: true
                    }

                    QQC2.Label {
                        text: backupStatus.lastError || root.statusText(backupStatus.state)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Kirigami.Heading {
                    visible: root.progress >= 0
                    text: (root.estimated ? "≈ " : "") + root.progress + "%"
                    level: 2
                }
            }

            QQC2.ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: Math.max(0, root.progress)
                indeterminate: root.running && root.progress < 0
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: Kirigami.Units.smallSpacing
                columnSpacing: Kirigami.Units.largeSpacing

                QQC2.Label { text: i18n("Profile:"); opacity: 0.7 }
                QQC2.Label {
                    text: backupStatus.profile
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }

                QQC2.Label { text: i18n("Status:"); opacity: 0.7 }
                QQC2.Label {
                    text: root.statusText(backupStatus.state)
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                QQC2.Label { text: i18n("Source:"); opacity: 0.7 }
                QQC2.Label {
                    text: backupStatus.currentSourceName || i18n("Unknown")
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }

                QQC2.Label { text: i18n("Destination:"); opacity: 0.7 }
                QQC2.Label {
                    text: backupStatus.targetName || i18n("Unknown")
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }

                QQC2.Label { text: i18n("Progress:"); opacity: 0.7 }
                QQC2.Label {
                    text: root.progress >= 0 ? (root.estimated ? "≈ " : "") + root.progress + "%" : i18n("Unknown")
                }

                QQC2.Label { text: i18n("Speed:"); opacity: 0.7 }
                QQC2.Label {
                    text: Number(backupStatus.speedBps) > 0
                        ? i18n("%1/s", root.formatBytes(backupStatus.speedBps))
                        : i18n("Unknown")
                }

                QQC2.Label { text: i18n("Time remaining:"); opacity: 0.7 }
                QQC2.Label { text: root.formatEta(backupStatus.etaSeconds) }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: backupStatus.errorCode.length > 0 || backupStatus.errorMessage.length > 0
                type: Kirigami.MessageType.Error
                text: root.statusText(backupStatus.state)
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true

                QQC2.Button {
                    text: i18n("Refresh")
                    icon.name: "view-refresh"
                    onClicked: backupStatus.start()
                }
            }
        }
    }
}
