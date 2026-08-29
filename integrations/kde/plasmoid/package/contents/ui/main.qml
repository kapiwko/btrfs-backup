// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import org.btrfsbackup.plasma

pragma ComponentBehavior: Bound

PlasmoidItem {
    id: root

    KI18n.KI18nContext {
        id: translations
        translationDomain: "plasma_applet_org.btrfsbackup.plasmoid"
    }

    property bool running: backupStatus.state === "running" || backupStatus.state === "starting" || backupStatus.state === "validating"
    property bool failed: backupStatus.state === "failed"
    property bool estimated: backupStatus.progressAccuracy === "estimated"
    property int progress: backupStatus.overallProgress
    property int relativeTimeTick: 0
    property string badgeIcon: {
        switch (backupStatus.state) {
        case "succeeded": return "emblem-ok-symbolic"
        case "failed": return "dialog-error-symbolic"
        case "cancelled": return "process-stop-symbolic"
        case "skipped": return "emblem-pause"
        default: return ""
        }
    }
    property int badgeType: {
        switch (backupStatus.state) {
        case "succeeded": return Kirigami.Badge.Type.Positive
        case "failed": return Kirigami.Badge.Type.Error
        case "cancelled": return Kirigami.Badge.Type.Warning
        default: return Kirigami.Badge.Type.Information
        }
    }

    Plasmoid.status: root.running || root.failed ? PlasmaCore.Types.ActiveStatus : PlasmaCore.Types.PassiveStatus
    toolTipMainText: translations.i18n("Btrfs Backups")
    toolTipSubText: backupStatus.lastError || root.activityText(backupStatus.activity, backupStatus.phase)

    BackupStatusModel {
        id: backupStatus
        profile: "default"
        Component.onCompleted: start()
    }

    Timer {
        interval: 60000
        running: root.visible
        repeat: true
        onTriggered: root.relativeTimeTick++
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
            return translations.i18n("Unknown")
        var minutes = Math.floor(seconds / 60)
        seconds = Math.floor(seconds % 60)
        if (minutes > 0)
            return translations.i18n("%1 min %2 sec", minutes, seconds)
        return translations.i18n("%1 sec", seconds)
    }

    function statusText(state) {
        switch (state) {
        case "starting":
        case "running":
            return translations.i18n("Backup is in progress")
        case "validating":
            return translations.i18n("Target validation is in progress")
        case "validated":
            return translations.i18n("Validation completed successfully")
        case "succeeded":
            return translations.i18n("Backup completed successfully")
        case "failed":
            return translations.i18n("Backup failed")
        case "cancelled":
            return translations.i18n("Backup cancelled")
        case "skipped":
            return translations.i18n("Backup skipped")
        default:
            return translations.i18n("No active backup")
        }
    }

    function activityText(activity, phase) {
        if (!root.running)
            return root.statusText(backupStatus.state)
        switch (activity) {
        case "sizing":
            return translations.i18n("Calculating transfer size")
        case "transferring":
            return translations.i18n("Transferring backup data")
        case "finalizing":
            return root.phaseText(phase)
        default:
            return root.phaseText(phase)
        }
    }

    function phaseText(phase) {
        switch (phase) {
        case "run-started": return translations.i18n("Starting backup")
        case "source-started": return translations.i18n("Preparing backup source")
        case "recover-pending": return translations.i18n("Recovering interrupted backup")
        case "cleanup-incoming": return translations.i18n("Cleaning temporary data")
        case "before-snapshot-hook": return translations.i18n("Running pre-snapshot hooks")
        case "create-snapshot": return translations.i18n("Creating local snapshot")
        case "after-snapshot-hook": return translations.i18n("Running post-snapshot hooks")
        case "send-receive": return translations.i18n("Preparing data transfer")
        case "sizing": return translations.i18n("Calculating transfer size")
        case "transferring": return translations.i18n("Transferring backup data")
        case "verify-received": return translations.i18n("Verifying received snapshot")
        case "commit-received": return translations.i18n("Committing received snapshot")
        case "apply-remote-retention": return translations.i18n("Applying target retention")
        case "apply-local-retention": return translations.i18n("Applying local retention")
        case "cleanup-source": return translations.i18n("Cleaning backup source")
        case "source-completed": return translations.i18n("Finalizing backup")
        case "validating-target": return translations.i18n("Validating backup target")
        default: return translations.i18n("Preparing backup")
        }
    }

    function targetStateText(state) {
        switch (state) {
        case "mounted": return translations.i18n("Mounted")
        case "unlocked": return translations.i18n("Unlocked")
        case "connected": return backupStatus.safeToRemove
            ? translations.i18n("Safe to remove")
            : translations.i18n("Connected")
        case "disconnected": return translations.i18n("Disconnected")
        default: return translations.i18n("Unknown")
        }
    }

    function historyText(state) {
        return root.statusText(state)
    }

    function relativeTime(value) {
        root.relativeTimeTick
        var timestamp = Date.parse(value)
        if (isNaN(timestamp))
            return translations.i18n("Unknown")
        var seconds = Math.max(0, Math.floor((Date.now() - timestamp) / 1000))
        if (seconds < 60)
            return translations.i18n("Just now")
        var minutes = Math.floor(seconds / 60)
        if (minutes < 60)
            return translations.i18np("1 minute ago", "%1 minutes ago", minutes)
        var hours = Math.floor(minutes / 60)
        if (hours < 24)
            return translations.i18np("1 hour ago", "%1 hours ago", hours)
        var days = Math.floor(hours / 24)
        if (days < 30)
            return translations.i18np("1 day ago", "%1 days ago", days)
        var months = Math.floor(days / 30)
        if (months < 12)
            return translations.i18np("1 month ago", "%1 months ago", months)
        var years = Math.floor(days / 365)
        return translations.i18np("1 year ago", "%1 years ago", years)
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
            opacity: backupStatus.managerConnected ? 1 : 0.65
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

        Item {
            id: compactIndeterminateProgress
            anchors.fill: parent
            visible: root.running && root.progress < 0

            Canvas {
                anchors.fill: parent
                onPaint: {
                    var context = getContext("2d")
                    context.clearRect(0, 0, width, height)
                    context.lineWidth = Math.max(2, width * 0.09)
                    context.lineCap = "round"
                    context.strokeStyle = Kirigami.Theme.highlightColor
                    context.beginPath()
                    context.arc(width / 2, height / 2, Math.min(width, height) / 2 - context.lineWidth / 2,
                                -Math.PI / 2, Math.PI / 3)
                    context.stroke()
                }
            }

            RotationAnimator on rotation {
                running: compactIndeterminateProgress.visible
                from: 0
                to: 360
                duration: 900
                loops: Animation.Infinite
            }
        }

        Kirigami.Badge {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: Math.max(10, parent.width * 0.46)
            height: width
            z: 2
            visible: backupStatus.managerConnected && root.badgeIcon.length > 0
            type: root.badgeType
            icon.name: root.badgeIcon
            icon.width: Math.max(8, width * 0.62)
            icon.height: icon.width
            padding: Math.max(1, width * 0.1)
        }
    }

    fullRepresentation: Item {
        implicitWidth: Kirigami.Units.gridUnit * 22
        implicitHeight: Kirigami.Units.gridUnit * 21

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
                        text: translations.i18n("Btrfs Backups")
                        level: 2
                        Layout.fillWidth: true
                    }

                    QQC2.Label {
                        text: backupStatus.lastError || root.activityText(backupStatus.activity, backupStatus.phase)
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

            PlasmaComponents3.ScrollView {
                id: detailsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                contentWidth: availableWidth
                PlasmaComponents3.ScrollBar.horizontal.policy: PlasmaComponents3.ScrollBar.AlwaysOff

                contentItem: Flickable {
                    id: detailsFlickable
                    contentWidth: width
                    contentHeight: detailsColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: detailsColumn
                        width: detailsFlickable.width
                        spacing: Kirigami.Units.largeSpacing

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: Kirigami.Units.smallSpacing
                columnSpacing: Kirigami.Units.largeSpacing

                QQC2.Label { text: translations.i18n("Profile:"); opacity: 0.7 }
                QQC2.ComboBox {
                    id: profileSelector
                    model: backupStatus.profiles
                    textRole: "name"
                    valueRole: "profileId"
                    Layout.fillWidth: true
                    enabled: !backupStatus.operationPending && count > 1
                    onActivated: backupStatus.profile = currentValue
                    Component.onCompleted: syncProfile()
                    function syncProfile() {
                        for (var index = 0; index < count; ++index) {
                            if (valueAt(index) === backupStatus.profile) {
                                currentIndex = index
                                return
                            }
                        }
                    }
                    Connections {
                        target: backupStatus
                        function onProfilesChanged() { profileSelector.syncProfile() }
                        function onProfileChanged() { profileSelector.syncProfile() }
                    }
                }

                QQC2.Label { text: translations.i18n("Status:"); opacity: 0.7 }
                QQC2.Label {
                    text: root.activityText(backupStatus.activity, backupStatus.phase)
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                QQC2.Label { text: translations.i18n("Source:"); opacity: 0.7 }
                QQC2.Label {
                    text: backupStatus.currentSourceName || translations.i18n("Unknown")
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }

                QQC2.Label { text: translations.i18n("Destination:"); opacity: 0.7 }
                QQC2.Label {
                    text: backupStatus.targetName || translations.i18n("Unknown")
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }

                QQC2.Label { text: translations.i18n("Target state:"); opacity: 0.7 }
                QQC2.Label {
                    text: root.targetStateText(backupStatus.targetState)
                    Layout.fillWidth: true
                }

                QQC2.Label { text: translations.i18n("Progress:"); opacity: 0.7 }
                QQC2.Label {
                    text: root.progress >= 0 ? (root.estimated ? "≈ " : "") + root.progress + "%" : translations.i18n("Unknown")
                }

                QQC2.Label { text: translations.i18n("Speed:"); opacity: 0.7 }
                QQC2.Label {
                    text: Number(backupStatus.speedBps) > 0
                        ? translations.i18n("%1/s", root.formatBytes(backupStatus.speedBps))
                        : translations.i18n("Unknown")
                }

                QQC2.Label { text: translations.i18n("Time remaining:"); opacity: 0.7 }
                QQC2.Label { text: root.formatEta(backupStatus.etaSeconds) }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: backupStatus.errorCode.length > 0
                type: Kirigami.MessageType.Error
                text: root.statusText(backupStatus.state)
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: backupStatus.lastOperation.length > 0 && !backupStatus.lastError
                type: Kirigami.MessageType.Positive
                text: translations.i18n("Operation accepted")
            }

            Kirigami.Separator { Layout.fillWidth: true }

            Kirigami.Heading {
                text: translations.i18n("Recent backups")
                level: 3
                visible: backupStatus.history.length > 0
            }

            Repeater {
                model: backupStatus.history
                delegate: RowLayout {
                    id: historyRow
                    required property var modelData
                    Layout.fillWidth: true

                    Kirigami.Icon {
                        source: historyRow.modelData.state === "succeeded" ? "emblem-ok-symbolic"
                            : historyRow.modelData.state === "failed" ? "dialog-error"
                            : "dialog-information"
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: implicitWidth
                    }
                    QQC2.Label {
                        text: root.historyText(historyRow.modelData.state)
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        text: root.relativeTime(historyRow.modelData.finishedAt)
                        opacity: 0.7
                        Layout.maximumWidth: Kirigami.Units.gridUnit * 8
                        elide: Text.ElideRight
                        QQC2.ToolTip.text: historyRow.modelData.finishedAt
                        QQC2.ToolTip.visible: historyDateHover.hovered
                        HoverHandler { id: historyDateHover }
                    }
                    }
                }
            }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                QQC2.Button {
                    text: translations.i18n("Start backup")
                    icon.name: "media-playback-start"
                    enabled: backupStatus.managerConnected && !root.running && !backupStatus.operationPending
                    onClicked: backupStatus.startBackup()
                }

                QQC2.ToolButton {
                    icon.name: "process-stop"
                    visible: root.running
                    enabled: backupStatus.canCancel && !backupStatus.operationPending
                    onClicked: backupStatus.cancelBackup()
                    QQC2.ToolTip.text: translations.i18n("Cancel")
                    QQC2.ToolTip.visible: hovered
                }

                QQC2.ToolButton {
                    icon.name: "task-complete"
                    enabled: backupStatus.managerConnected && !root.running && !backupStatus.operationPending
                    onClicked: backupStatus.validateTarget()
                    QQC2.ToolTip.text: translations.i18n("Validate")
                    QQC2.ToolTip.visible: hovered
                }

                QQC2.ToolButton {
                    icon.name: "media-eject"
                    enabled: backupStatus.managerConnected && !root.running
                        && (backupStatus.targetMounted || backupStatus.targetUnlocked)
                        && !backupStatus.operationPending
                    onClicked: backupStatus.ejectTarget()
                    QQC2.ToolTip.text: translations.i18n("Eject")
                    QQC2.ToolTip.visible: hovered
                }

                Item { Layout.fillWidth: true }

                QQC2.ToolButton {
                    icon.name: "view-refresh"
                    enabled: !backupStatus.operationPending
                    onClicked: backupStatus.refreshNow()
                    QQC2.ToolTip.text: translations.i18n("Refresh")
                    QQC2.ToolTip.visible: hovered
                }
            }
        }
    }
}
