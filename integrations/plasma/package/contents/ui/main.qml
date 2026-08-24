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
    toolTipMainText: backupStatus.profileName || qsTr("Kopie zapasowe Btrfs")
    toolTipSubText: backupStatus.message || backupStatus.lastError || qsTr("Brak aktywnej kopii")

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
            return "—"
        var minutes = Math.floor(seconds / 60)
        seconds = Math.floor(seconds % 60)
        if (minutes > 0)
            return minutes + " min " + seconds + " s"
        return seconds + " s"
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
            opacity: backupStatus.connected ? 1 : 0.65
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
                        text: backupStatus.profileName || qsTr("Kopie zapasowe Btrfs")
                        level: 2
                        Layout.fillWidth: true
                    }

                    QQC2.Label {
                        text: backupStatus.message || backupStatus.lastError || qsTr("Brak aktywnej kopii")
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

                QQC2.Label { text: qsTr("Profil:"); opacity: 0.7 }
                QQC2.Label {
                    text: backupStatus.profile
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }

                QQC2.Label { text: qsTr("Etap:"); opacity: 0.7 }
                QQC2.Label {
                    text: backupStatus.phase || "—"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                QQC2.Label { text: qsTr("Źródło:"); opacity: 0.7 }
                QQC2.Label {
                    text: backupStatus.currentSourceName || "—"
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }

                QQC2.Label { text: qsTr("Dane:"); opacity: 0.7 }
                QQC2.Label {
                    text: {
                        var value = root.formatBytes(backupStatus.bytesProcessed)
                        if (Number(backupStatus.bytesTotalEstimated) > 0)
                            value += " / " + (root.estimated ? "≈ " : "") + root.formatBytes(backupStatus.bytesTotalEstimated)
                        return value
                    }
                }

                QQC2.Label { text: qsTr("Prędkość:"); opacity: 0.7 }
                QQC2.Label { text: Number(backupStatus.speedBps) > 0 ? root.formatBytes(backupStatus.speedBps) + "/s" : "—" }

                QQC2.Label { text: qsTr("Pozostało:"); opacity: 0.7 }
                QQC2.Label { text: root.formatEta(backupStatus.etaSeconds) }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: backupStatus.errorCode.length > 0 || backupStatus.errorMessage.length > 0
                type: Kirigami.MessageType.Error
                text: backupStatus.errorMessage || backupStatus.errorCode
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true

                QQC2.Button {
                    text: qsTr("Odśwież")
                    icon.name: "view-refresh"
                    onClicked: backupStatus.start()
                }
            }
        }
    }
}
