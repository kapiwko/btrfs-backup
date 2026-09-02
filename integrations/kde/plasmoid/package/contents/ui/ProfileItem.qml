// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.extras as PlasmaExtras
import org.btrfsbackup.plasma

PlasmaExtras.ExpandableListItem {
    id: root

    required index
    required property string profileId
    required property string profileName
    required property string targetNameHint
    required property int relativeTimeTick
    property int refreshRevision: 0
    property int historyLimit: 3
    property bool autoExpandActive: true
    property bool autoExpandFailed: true
    property bool showStorageDetails: true
    property bool hideSourceNamesInTooltip: false
    property bool previousRunning: false
    property bool previousFailed: false
    property var statusModelOverride: null
    readonly property var profileStatus: statusModelOverride ?? liveProfileStatus
    readonly property var ejectProfileAction: ejectAction
    readonly property var browseProfileAction: browseAction

    readonly property bool running: profileStatus.run.state === "running"
        || profileStatus.run.state === "starting"
        || profileStatus.run.state === "validating"
    readonly property bool failed: profileStatus.run.state === "failed"
    readonly property int progress: profileStatus.run.overallProgress

    signal summaryUpdated(string profileId, int priority, bool isRunning, bool isFailed, int profileProgress, string subtitle)
    signal summaryRemoved(string profileId)

    KI18n.KI18nContext {
        id: translations
        translationDomain: "plasma_applet_org.btrfsbackup.plasmoid"
    }

    BackupStatusModel {
        id: liveProfileStatus
        profile: root.profileId
        historyLimit: root.historyLimit
        Component.onCompleted: {
            if (root.statusModelOverride === null)
                start()
        }
    }

    icon: root.targetIcon()
    iconEmblem: root.statusEmblem()
    title: root.profileName || root.profileId
    subtitle: root.subtitleText()
    subtitleCanWrap: true
    subtitleColor: root.failed || profileStatus.lastError.length > 0 || !profileStatus.configurationValid
        ? Kirigami.Theme.negativeTextColor
        : Kirigami.Theme.textColor
    isBusy: root.running || profileStatus.operationPending
    showDefaultActionButtonWhenBusy: root.running
    defaultActionButtonVisible: profileStatus.managerConnected
        && (root.running || profileStatus.target.connected)
    defaultActionButtonAction: QQC2.Action {
        enabled: !profileStatus.operationPending
            && (root.running ? profileStatus.run.canCancel : profileStatus.target.connected)
        icon.name: root.running ? "process-stop" : "media-playback-start"
        text: root.running ? translations.i18n("Cancel") : translations.i18n("Start backup")
        onTriggered: {
            if (root.running)
                profileStatus.cancelBackup()
            else
                profileStatus.startBackup()
        }
    }
    QQC2.Action {
        id: ejectAction
        enabled: profileStatus.managerConnected
            && profileStatus.target.connected
            && !root.running
            && !profileStatus.operationPending
            && (profileStatus.target.mounted || profileStatus.target.unlocked)
        icon.name: "media-eject"
        text: translations.i18n("Eject")
        onTriggered: profileStatus.ejectTarget()
    }

    QQC2.Action {
        id: browseAction
        enabled: profileStatus.managerConnected
            && profileStatus.target.connected
            && !profileStatus.operationPending
        icon.name: "folder-open-symbolic"
        text: translations.i18n("Browse backups")
        onTriggered: profileStatus.browseBackups()
    }

    customExpandedViewContent: Component {
        Item {
            implicitHeight: expandedLayout.implicitHeight

            ColumnLayout {
                id: expandedLayout

                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Kirigami.Units.smallSpacing * 2

                ProfileActions {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.gridUnit
                    Layout.rightMargin: Kirigami.Units.gridUnit
                    ejectAction: root.ejectProfileAction
                    browseAction: root.browseProfileAction
                    browseVisible: root.profileStatus.browseSupported
                }

                ProfileExpandedDetails {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.gridUnit
                    Layout.rightMargin: Kirigami.Units.gridUnit
                    profileStatus: root.profileStatus
                    targetNameHint: root.targetNameHint
                    running: root.running
                    progress: root.progress
                    showStorageDetails: root.showStorageDetails
                    activityLabel: root.activityText(root.profileStatus.run.activity, root.profileStatus.run.phase)
                    sourceLabel: root.sourceText()
                    statusLabel: root.statusText(root.profileStatus.run.state)
                    targetStateLabel: root.targetStateText(root.profileStatus.target.state)
                    lastSuccessLabel: root.lastSuccessText(root.profileStatus.run.lastSuccessAt)
                    relativeStorageTime: root.relativeTime(root.profileStatus.target.storageMeasuredAt)
                    etaLabel: root.formatEta(root.profileStatus.run.etaSeconds)
                    durationLabel: root.formatDuration(root.profileStatus.run.elapsedSeconds)
                    operationLabel: root.operationResultText(root.profileStatus.lastOperation)
                    historySummaryFor: entry => root.historySummary(entry)
                    relativeTimeFor: value => root.relativeTime(value)
                }
            }
        }
    }

    Connections {
        target: profileStatus
        function onStatusChanged() {
            root.applyAutomaticExpansion()
            root.publishSummary()
        }
        function onTargetChanged() { root.publishSummary() }
        function onErrorChanged() { root.publishSummary() }
        function onManagerConnectedChanged() { root.publishSummary() }
    }

    Component.onCompleted: publishSummary()
    Component.onDestruction: summaryRemoved(root.profileId)
    onRefreshRevisionChanged: profileStatus.refreshNow()

    function publishSummary() {
        root.summaryUpdated(root.profileId, root.summaryPriority(), root.running, root.failed, root.progress, root.subtitleText())
    }

    function targetIcon() {
        if (!profileStatus.target.connected)
            return "network-disconnect-symbolic"
        if (profileStatus.target.mounted)
            return "drive-harddisk-symbolic"
        if (profileStatus.target.unlocked)
            return "object-unlocked-symbolic"
        return "object-locked-symbolic"
    }

    function statusEmblem() {
        if (profileStatus.lastError.length > 0 || !profileStatus.configurationValid || root.failed)
            return "state-error-symbolic"
        if (root.running)
            return ""
        if (profileStatus.target.spaceBelowMinimum)
            return "state-warning-symbolic"
        if (profileStatus.target.safeToRemove)
            return "emblem-ok-symbolic"
        switch (profileStatus.run.state) {
        case "succeeded":
        case "validated": return "emblem-ok-symbolic"
        case "failed": return "state-error-symbolic"
        case "cancelled": return "state-pause-symbolic"
        default: return ""
        }
    }

    function summaryPriority() {
        if (profileStatus.lastError.length > 0 || !profileStatus.configurationValid || root.failed)
            return 1
        if (root.running)
            return 2
        if (profileStatus.target.spaceBelowMinimum)
            return 3
        if (profileStatus.target.safeToRemove)
            return 5
        if (profileStatus.run.state === "succeeded")
            return 6
        return 7
    }

    function subtitleText() {
        if (profileStatus.lastError.length > 0)
            return profileStatus.lastError
        if (!profileStatus.configurationValid)
            return root.configurationErrorText(profileStatus.configurationErrorCode)
        if (root.running) {
            let activity = root.activityText(profileStatus.run.activity, profileStatus.run.phase)
            if (root.progress >= 0)
                activity += translations.i18n(" (%1%)", root.progress)
            if (!root.hideSourceNamesInTooltip && profileStatus.run.sourceName.length > 0)
                return activity + " - " + profileStatus.run.sourceName
            return activity
        }
        const target = profileStatus.target.name || profileStatus.run.targetName || root.targetNameHint || translations.i18n("Backup target")
        return target + " - " + root.targetStateText(profileStatus.target.state)
    }

    function configurationErrorText(code) {
        switch (code) {
        case "configuration.source_missing": return translations.i18n("A backup source does not exist")
        case "configuration.source_not_subvolume": return translations.i18n("A backup source is not a Btrfs subvolume")
        default: return translations.i18n("A backup source cannot be inspected")
        }
    }

    function applyAutomaticExpansion() {
        if ((root.autoExpandActive && root.running && !root.previousRunning)
                || (root.autoExpandFailed && root.failed && !root.previousFailed))
            root.expand()
        root.previousRunning = root.running
        root.previousFailed = root.failed
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
        case "unexpected-mount": return translations.i18n("Unexpected mount")
        case "unlocked": return translations.i18n("Unlocked")
        case "connected": return profileStatus.target.safeToRemove
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
        case "profile-activation": return profileStatus.profileEnabled
            ? translations.i18n("Automatic backups enabled")
            : translations.i18n("Automatic backups disabled")
        default: return translations.i18n("Operation completed")
        }
    }

    function historyText(state) {
        return root.statusText(state)
    }

    function historySummary(entry) {
        let parts = [root.historyText(entry.state)]
        if (entry.durationSeconds >= 0)
            parts.push(root.formatDuration(entry.durationSeconds))
        if (entry.sourceCount > 0)
            parts.push(translations.i18np("1 source", "%1 sources", entry.sourceCount))
        if (entry.errorCode?.length > 0)
            parts.push(entry.errorCode)
        return parts.join(" · ")
    }

    function sourceText() {
        const name = profileStatus.run.sourceName || translations.i18n("Unknown")
        if (profileStatus.run.sourceIndex <= 0 || profileStatus.run.sourceCount <= 0)
            return name
        return translations.i18n("%1 (%2 of %3)", name,
                                 profileStatus.run.sourceIndex,
                                 profileStatus.run.sourceCount)
    }

    function formatDuration(value) {
        let seconds = Number(value)
        if (seconds < 0)
            return translations.i18n("Unknown")
        const hours = Math.floor(seconds / 3600)
        const minutes = Math.floor((seconds % 3600) / 60)
        if (hours > 0)
            return translations.i18n("%1 h %2 min", hours, minutes)
        if (minutes > 0)
            return translations.i18np("1 minute", "%1 minutes", minutes)
        return translations.i18np("1 second", "%1 seconds", Math.max(1, Math.floor(seconds)))
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

    function lastSuccessText(value) {
        const timestamp = Date.parse(value)
        if (isNaN(timestamp))
            return translations.i18n("No successful backup")
        const completed = new Date(timestamp)
        const now = new Date()
        if (completed.getFullYear() === now.getFullYear()
                && completed.getMonth() === now.getMonth()
                && completed.getDate() === now.getDate()) {
            return translations.i18n("today, %1", Qt.formatTime(completed, "HH:mm"))
        }
        return root.relativeTime(value)
    }
}
