// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras
import org.btrfsbackup.plasma

PlasmaExtras.ExpandableListItem {
    id: root

    required index
    required property string profileId
    required property string profileName
    required property string targetNameHint
    required property int relativeTimeTick
    required property int refreshRevision

    readonly property bool running: profileStatus.state === "running"
        || profileStatus.state === "starting"
        || profileStatus.state === "validating"
    readonly property bool failed: profileStatus.state === "failed"
    readonly property int progress: profileStatus.overallProgress
    readonly property int historyCount: profileStatus.history.length

    signal summaryUpdated(string profileId, bool isRunning, bool isFailed, int profileProgress, string subtitle)
    signal summaryRemoved(string profileId)

    KI18n.KI18nContext {
        id: translations
        translationDomain: "plasma_applet_org.btrfsbackup.plasmoid"
    }

    BackupStatusModel {
        id: profileStatus
        profile: root.profileId
        Component.onCompleted: start()
    }

    icon: root.targetIcon()
    iconEmblem: root.statusEmblem()
    title: root.profileName || root.profileId
    subtitle: root.subtitleText()
    subtitleCanWrap: true
    subtitleColor: root.failed || profileStatus.lastError.length > 0
        ? Kirigami.Theme.negativeTextColor
        : Kirigami.Theme.textColor
    isBusy: profileStatus.operationPending
    showDefaultActionButtonWhenBusy: false
    defaultActionButtonVisible: profileStatus.managerConnected
        && (root.running || profileStatus.targetConnected)
    defaultActionButtonAction: QQC2.Action {
        enabled: !profileStatus.operationPending
            && (root.running ? profileStatus.canCancel : profileStatus.targetConnected)
        icon.name: root.running ? "process-stop" : "media-playback-start"
        text: root.running ? translations.i18n("Cancel") : translations.i18n("Start backup")
        onTriggered: {
            if (root.running)
                profileStatus.cancelBackup()
            else
                profileStatus.startBackup()
        }
    }
    contextualActions: [
        QQC2.Action {
            enabled: profileStatus.managerConnected
                && profileStatus.targetConnected
                && !root.running
                && !profileStatus.operationPending
            icon.name: "task-complete"
            text: translations.i18n("Validate")
            onTriggered: profileStatus.validateTarget()
        },
        QQC2.Action {
            enabled: profileStatus.managerConnected
                && profileStatus.targetConnected
                && !root.running
                && !profileStatus.operationPending
                && (profileStatus.targetMounted || profileStatus.targetUnlocked)
            icon.name: "media-eject"
            text: translations.i18n("Eject")
            onTriggered: profileStatus.ejectTarget()
        }
    ]

    customExpandedViewContent: Component {
        ColumnLayout {
            width: root.width
            spacing: Kirigami.Units.smallSpacing

            TransferSpeedChart {
                Layout.fillWidth: true
                visible: root.running
                active: root.running
                currentSpeed: profileStatus.speedBps
            }

            PlasmaComponents3.ProgressBar {
                Layout.fillWidth: true
                visible: root.running
                from: 0
                to: 100
                value: Math.max(0, root.progress)
                indeterminate: root.progress < 0
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: Kirigami.Units.smallSpacing / 4
                columnSpacing: Kirigami.Units.smallSpacing

                PlasmaComponents3.Label {
                    text: translations.i18n("Status:")
                    horizontalAlignment: Text.AlignRight
                    font: Kirigami.Theme.smallFont
                    opacity: 0.6
                }
                PlasmaComponents3.Label {
                    text: root.running
                        ? root.activityText(profileStatus.activity, profileStatus.phase)
                        : root.statusText(profileStatus.state)
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                }

                PlasmaComponents3.Label {
                    text: translations.i18n("Backup target:")
                    horizontalAlignment: Text.AlignRight
                    font: Kirigami.Theme.smallFont
                    opacity: 0.6
                }
                PlasmaComponents3.Label {
                    text: profileStatus.targetName || root.targetNameHint || translations.i18n("Unknown")
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font: Kirigami.Theme.smallFont
                }

                PlasmaComponents3.Label {
                    text: translations.i18n("Target state:")
                    horizontalAlignment: Text.AlignRight
                    font: Kirigami.Theme.smallFont
                    opacity: 0.6
                }
                PlasmaComponents3.Label {
                    text: root.targetStateText(profileStatus.targetState)
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                }

                PlasmaComponents3.Label {
                    visible: root.running
                    text: translations.i18n("Source:")
                    horizontalAlignment: Text.AlignRight
                    font: Kirigami.Theme.smallFont
                    opacity: 0.6
                }
                PlasmaComponents3.Label {
                    visible: root.running
                    text: profileStatus.currentSourceName || translations.i18n("Unknown")
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font: Kirigami.Theme.smallFont
                }

                PlasmaComponents3.Label {
                    visible: root.running
                    text: translations.i18n("Time remaining:")
                    horizontalAlignment: Text.AlignRight
                    font: Kirigami.Theme.smallFont
                    opacity: 0.6
                }
                PlasmaComponents3.Label {
                    visible: root.running
                    text: root.formatEta(profileStatus.etaSeconds)
                    Layout.fillWidth: true
                    font: Kirigami.Theme.smallFont
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: profileStatus.lastError.length > 0 || profileStatus.errorCode.length > 0
                type: Kirigami.MessageType.Error
                text: profileStatus.lastError || root.statusText(profileStatus.state)
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: profileStatus.lastOperation.length > 0 && profileStatus.lastError.length === 0
                type: Kirigami.MessageType.Positive
                text: root.operationResultText(profileStatus.lastOperation)
            }

            PlasmaExtras.ListSectionHeader {
                Layout.fillWidth: true
                visible: profileStatus.history.length > 0
                text: translations.i18n("Recent backups")
            }

            Repeater {
                model: profileStatus.history

                delegate: RowLayout {
                    id: historyRow
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: historyRow.modelData.state === "succeeded" ? "emblem-ok-symbolic"
                            : historyRow.modelData.state === "failed" ? "dialog-error-symbolic"
                            : "dialog-information-symbolic"
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: implicitWidth
                    }
                    PlasmaComponents3.Label {
                        text: root.historyText(historyRow.modelData.state)
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font: Kirigami.Theme.smallFont
                    }
                    PlasmaComponents3.Label {
                        text: root.relativeTime(historyRow.modelData.finishedAt)
                        opacity: 0.7
                        Layout.maximumWidth: Kirigami.Units.gridUnit * 8
                        elide: Text.ElideRight
                        font: Kirigami.Theme.smallFont

                        PlasmaComponents3.ToolTip {
                            text: historyRow.modelData.finishedAt
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: profileStatus
        function onStatusChanged() { root.publishSummary() }
        function onTargetChanged() { root.publishSummary() }
        function onErrorChanged() { root.publishSummary() }
        function onManagerConnectedChanged() { root.publishSummary() }
    }

    Component.onCompleted: publishSummary()
    Component.onDestruction: summaryRemoved(root.profileId)
    onRefreshRevisionChanged: profileStatus.refreshNow()

    function refresh() {
        profileStatus.refreshNow()
    }

    function publishSummary() {
        root.summaryUpdated(root.profileId, root.running, root.failed, root.progress, root.subtitleText())
    }

    function targetIcon() {
        if (!profileStatus.targetConnected)
            return "drive-removable-media-symbolic"
        if (profileStatus.targetMounted)
            return "drive-harddisk-symbolic"
        return "drive-removable-media-symbolic"
    }

    function statusEmblem() {
        switch (profileStatus.state) {
        case "succeeded":
        case "validated": return "emblem-ok-symbolic"
        case "failed": return "emblem-error"
        case "cancelled": return "emblem-pause"
        default: return ""
        }
    }

    function subtitleText() {
        if (profileStatus.lastError.length > 0)
            return profileStatus.lastError
        if (root.running)
            return root.activityText(profileStatus.activity, profileStatus.phase)
        const target = profileStatus.targetName || root.targetNameHint || translations.i18n("Backup target")
        return target + " - " + root.targetStateText(profileStatus.targetState)
    }

    function statusText(state) {
        switch (state) {
        case "starting":
        case "running": return translations.i18n("Backup is in progress")
        case "validating": return translations.i18n("Target validation is in progress")
        case "validated": return translations.i18n("Validation completed successfully")
        case "succeeded": return translations.i18n("Backup completed successfully")
        case "failed": return translations.i18n("Backup failed")
        case "cancelled": return translations.i18n("Backup cancelled")
        case "skipped": return translations.i18n("Backup skipped")
        default: return translations.i18n("No active backup")
        }
    }

    function activityText(activity, phase) {
        switch (activity) {
        case "sizing": return translations.i18n("Calculating transfer size")
        case "transferring": return translations.i18n("Transferring backup data")
        default: return root.phaseText(phase)
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
        case "validated": return translations.i18n("Validation completed successfully")
        default: return translations.i18n("Preparing backup")
        }
    }

    function targetStateText(state) {
        switch (state) {
        case "mounted": return translations.i18n("Mounted")
        case "unlocked": return translations.i18n("Unlocked")
        case "connected": return profileStatus.safeToRemove
            ? translations.i18n("Safe to remove")
            : translations.i18n("Connected")
        case "disconnected": return translations.i18n("Disconnected")
        default: return translations.i18n("Unknown")
        }
    }

    function operationResultText(operation) {
        switch (operation) {
        case "start-backup": return translations.i18n("Backup started")
        case "cancel-backup": return translations.i18n("Cancellation requested")
        case "validate-target": return translations.i18n("Validation completed successfully")
        case "eject-target": return translations.i18n("Target ejected safely")
        default: return translations.i18n("Operation completed")
        }
    }

    function historyText(state) {
        return root.statusText(state)
    }

    function formatEta(value) {
        let seconds = Number(value || -1)
        if (seconds < 0)
            return translations.i18n("Unknown")
        const minutes = Math.floor(seconds / 60)
        seconds = Math.floor(seconds % 60)
        if (minutes > 0)
            return translations.i18n("%1 min %2 sec", minutes, seconds)
        return translations.i18n("%1 sec", seconds)
    }

    function relativeTime(value) {
        root.relativeTimeTick
        const timestamp = Date.parse(value)
        if (isNaN(timestamp))
            return translations.i18n("Unknown")
        const seconds = Math.max(0, Math.floor((Date.now() - timestamp) / 1000))
        if (seconds < 60)
            return translations.i18n("Just now")
        const minutes = Math.floor(seconds / 60)
        if (minutes < 60)
            return translations.i18np("1 minute ago", "%1 minutes ago", minutes)
        const hours = Math.floor(minutes / 60)
        if (hours < 24)
            return translations.i18np("1 hour ago", "%1 hours ago", hours)
        const days = Math.floor(hours / 24)
        if (days < 30)
            return translations.i18np("1 day ago", "%1 days ago", days)
        const months = Math.floor(days / 30)
        if (months < 12)
            return translations.i18np("1 month ago", "%1 months ago", months)
        const years = Math.floor(days / 365)
        return translations.i18np("1 year ago", "%1 years ago", years)
    }
}
