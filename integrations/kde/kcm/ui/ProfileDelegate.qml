// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.btrfsbackup.kde
import org.btrfsbackup.kde as BtrfsBackup

QQC2.ItemDelegate {
    id: delegate

    required property int index
    required property var modelData
    required property var directory
    required property var statusOverride
    required property var profileSummaryFor
    readonly property var profileStatus: statusOverride ?? liveProfileStatus

    signal detailsRequested(string profileId)
    signal editRequested(string profileId)

    width: ListView.view?.width ?? implicitWidth
    highlighted: pressed
    Kirigami.Theme.useAlternateBackgroundColor: true
    onClicked: {
        if (ListView.view)
            ListView.view.currentIndex = index
        detailsRequested(modelData.profileId)
    }

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    BackupStatusModel {
        id: liveProfileStatus
        profile: delegate.modelData.profileId
        directory: delegate.directory
        historyLimit: 5
        Component.onCompleted: {
            if (delegate.statusOverride === null)
                start()
        }
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Item {
                implicitWidth: Kirigami.Units.iconSizes.medium
                implicitHeight: implicitWidth

                Kirigami.Icon {
                    anchors.fill: parent
                    source: "drive-harddisk-symbolic"
                }
                Kirigami.Icon {
                    objectName: "profileTargetStateIndicator"
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    width: Kirigami.Units.iconSizes.small
                    height: width
                    source: delegate.targetIndicatorIcon()

                    HoverHandler { id: targetIndicatorHover }
                    QQC2.ToolTip.visible: targetIndicatorHover.hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    QQC2.ToolTip.text: delegate.targetIndicatorText()
                }
            }

            Kirigami.TitleSubtitleWithActions {
                Layout.fillWidth: true
                title: delegate.modelData.name || delegate.modelData.profileId
                subtitle: delegate.profileSummaryFor(delegate.profileStatus, delegate.modelData)
                selected: false
                displayHint: QQC2.Button.IconOnly
                actions: [
                    Kirigami.Action {
                        icon.name: "document-edit-symbolic"
                        text: translations.i18n("Edit profile")
                        tooltip: text
                        enabled: !delegate.profileStatus.operationPending
                        onTriggered: delegate.editRequested(delegate.modelData.profileId)
                    },
                    Kirigami.Action {
                        icon.name: "folder-open-symbolic"
                        text: translations.i18n("Browse backups")
                        tooltip: text
                        visible: delegate.profileStatus.browseSupported
                        enabled: BtrfsBackup.ProfilePresentation.canBrowse(delegate.profileStatus)
                        onTriggered: delegate.profileStatus.browseBackups()
                    },
                    Kirigami.Action {
                        icon.name: "media-playback-start-symbolic"
                        text: translations.i18n("Start backup")
                        tooltip: text
                        visible: !BtrfsBackup.ProfilePresentation.isRunning(delegate.profileStatus.run.state)
                        enabled: BtrfsBackup.ProfilePresentation.canStart(delegate.profileStatus)
                        onTriggered: delegate.profileStatus.startBackup()
                    },
                    Kirigami.Action {
                        icon.name: "media-playback-stop-symbolic"
                        text: translations.i18n("Cancel backup")
                        tooltip: text
                        visible: BtrfsBackup.ProfilePresentation.isRunning(delegate.profileStatus.run.state)
                        enabled: BtrfsBackup.ProfilePresentation.canCancel(delegate.profileStatus)
                        onTriggered: delegate.profileStatus.cancelBackup()
                    },
                    Kirigami.Action {
                        icon.name: "media-eject-symbolic"
                        text: translations.i18n("Eject")
                        tooltip: text
                        visible: delegate.profileStatus.target.connected
                        enabled: BtrfsBackup.ProfilePresentation.canEject(delegate.profileStatus)
                        onTriggered: delegate.profileStatus.ejectTarget()
                    }
                ]
            }

            QQC2.BusyIndicator {
                running: delegate.profileStatus.operationPending
                    || BtrfsBackup.ProfilePresentation.isRunning(delegate.profileStatus.run.state)
                visible: running
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: implicitWidth
            }

            QQC2.Switch {
                text: translations.i18n("Automatic backups")
                checked: delegate.profileStatus.profileEnabled
                enabled: BtrfsBackup.ProfilePresentation.canToggleAutomatic(delegate.profileStatus)
                onToggled: {
                    if (checked !== delegate.profileStatus.profileEnabled)
                        delegate.profileStatus.setProfileEnabled(checked)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.iconSizes.medium
                + Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing
            visible: delegate.profileStatus.target.storageKnown

            QQC2.ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: Math.max(0, Math.min(100,
                    delegate.profileStatus.target.usagePercent))
            }
            Kirigami.Icon {
                visible: delegate.profileStatus.target.spaceBelowMinimum
                source: "dialog-warning-symbolic"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: implicitWidth
                color: Kirigami.Theme.neutralTextColor
            }
            QQC2.Label {
                text: delegate.profileStatus.target.spaceBelowMinimum
                    ? translations.i18n("Low free space: %1 available",
                        delegate.profileStatus.target.availableText)
                    : translations.i18n("%1 available",
                        delegate.profileStatus.target.availableText)
                color: delegate.profileStatus.target.spaceBelowMinimum
                    ? Kirigami.Theme.neutralTextColor
                    : Kirigami.Theme.textColor
            }
        }

    }

    function targetIndicatorIcon() {
        return BtrfsBackup.ProfilePresentation.statusIcon(delegate.profileStatus)
    }

    function targetIndicatorText() {
        if (!delegate.profileStatus.configurationValid)
            return translations.i18n("Backup failed")
        switch (delegate.profileStatus.target.state) {
        case "mounted": return translations.i18n("Mounted")
        case "unexpected-mount": return translations.i18n("Unexpected mount")
        case "unlocked": return translations.i18n("Unlocked")
        case "connected": return translations.i18n("Connected")
        case "disconnected": return translations.i18n("Disconnected")
        default: return translations.i18n("Unknown")
        }
    }
}
