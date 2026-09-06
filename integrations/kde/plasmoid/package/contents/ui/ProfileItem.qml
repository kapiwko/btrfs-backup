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
import org.btrfsbackup.kde
import org.btrfsbackup.kde as BtrfsBackup

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
    property bool statusProvidedExternally: false
    property var statusModelOverride: null
    readonly property var profileStatus: statusModelOverride ?? liveProfileStatus
    readonly property var ejectProfileAction: ejectAction
    readonly property var browseProfileAction: browseAction

    readonly property bool running: BtrfsBackup.ProfilePresentation.isRunning(profileStatus.run.state)
    readonly property bool failed: profileStatus.run.state === "failed"
    readonly property int progress: profileStatus.run.overallProgress

    signal summaryUpdated(string profileId, string profileName, int priority, bool isRunning, bool isFailed, int profileProgress,
                          int attentionPriority, string attentionIcon, string subtitle)
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
            if (!root.statusProvidedExternally)
                start()
        }
    }

    icon: "drive-harddisk-symbolic"
    iconEmblem: BtrfsBackup.ProfilePresentation.statusIcon(profileStatus)
    title: root.profileName || root.profileId
    subtitle: root.subtitleText()
    subtitleCanWrap: true
    subtitleColor: root.failed || profileStatus.lastError.length > 0 || !profileStatus.configurationValid
        ? Kirigami.Theme.negativeTextColor
        : Kirigami.Theme.textColor
    isBusy: root.running || profileStatus.operationPending
    showDefaultActionButtonWhenBusy: root.running
    defaultActionButtonVisible: BtrfsBackup.ProfilePresentation.primaryActionVisible(profileStatus)
    defaultActionButtonAction: QQC2.Action {
        enabled: BtrfsBackup.ProfilePresentation.primaryActionEnabled(profileStatus)
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
        enabled: BtrfsBackup.ProfilePresentation.canEject(profileStatus)
        icon.name: "media-eject"
        text: translations.i18n("Eject")
        onTriggered: profileStatus.ejectTarget()
    }

    QQC2.Action {
        id: browseAction
        enabled: BtrfsBackup.ProfilePresentation.canBrowse(profileStatus)
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
                    activityLabel: BtrfsBackup.ProfilePresentation.activityText(translations, root.profileStatus.run.activity, root.profileStatus.run.phase)
                    sourceLabel: BtrfsBackup.ProfilePresentation.sourceText(translations, root.profileStatus.run)
                    statusLabel: BtrfsBackup.ProfilePresentation.statusText(translations, root.profileStatus.run.state)
                    targetStateLabel: BtrfsBackup.ProfilePresentation.targetStateText(translations, root.profileStatus.target.state, root.profileStatus.target.safeToRemove)
                    lastSuccessLabel: BtrfsBackup.ProfilePresentation.lastSuccessText(translations, root.profileStatus.run.lastSuccessAt, root.relativeTimeTick)
                    etaLabel: BtrfsBackup.ProfilePresentation.formatEta(translations, root.profileStatus.run.etaSeconds)
                    durationLabel: BtrfsBackup.ProfilePresentation.formatDuration(translations, root.profileStatus.run.elapsedSeconds)
                    operationLabel: BtrfsBackup.ProfilePresentation.operationResultText(translations, root.profileStatus.lastOperation, root.profileStatus.profileEnabled)
                    historySummaryFor: entry => BtrfsBackup.ProfilePresentation.historySummary(translations, entry)
                    relativeTimeFor: value => BtrfsBackup.ProfilePresentation.relativeTime(translations, value, root.relativeTimeTick)
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
        root.summaryUpdated(root.profileId, root.profileName || root.profileId,
                           BtrfsBackup.ProfilePresentation.summaryPriority(profileStatus), root.running, root.failed, root.progress,
                           BtrfsBackup.ProfilePresentation.attentionPriority(profileStatus),
                           BtrfsBackup.ProfilePresentation.attentionIcon(profileStatus), root.subtitleText())
    }

    function subtitleText() {
        if (profileStatus.lastError.length > 0)
            return profileStatus.lastError
        if (!profileStatus.configurationValid)
            return BtrfsBackup.ProfilePresentation.configurationErrorText(translations, profileStatus.configurationErrorCode)
        if (root.running) {
            let activity = BtrfsBackup.ProfilePresentation.activityText(translations, profileStatus.run.activity, profileStatus.run.phase)
            if (root.progress >= 0)
                activity += translations.i18n(" (%1%)", root.progress)
            if (!root.hideSourceNamesInTooltip && profileStatus.run.sourceName.length > 0)
                return activity + " - " + profileStatus.run.sourceName
            return activity
        }
        const target = profileStatus.target.name || profileStatus.run.targetName || root.targetNameHint || translations.i18n("Backup target")
        return target + " - " + BtrfsBackup.ProfilePresentation.targetStateText(
            translations, profileStatus.target.state, profileStatus.target.safeToRemove)
    }

    function applyAutomaticExpansion() {
        if ((root.autoExpandActive && root.running && !root.previousRunning)
                || (root.autoExpandFailed && root.failed && !root.previousFailed))
            root.expand()
        root.previousRunning = root.running
        root.previousFailed = root.failed
    }

}
