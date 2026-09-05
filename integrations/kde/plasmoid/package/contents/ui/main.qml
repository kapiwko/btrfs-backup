// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasmoid
import org.btrfsbackup.kde
import org.btrfsbackup.kde as BtrfsBackup

PlasmoidItem {
    id: root

    property int relativeTimeTick: 0
    property int refreshRevision: 0
    property var profileSummaries: ({})
    readonly property var displayedProfiles: {
        const visibleSetting = Plasmoid.configuration.visibleProfiles
        const visible = visibleSetting === "*" ? null
            : (visibleSetting.length > 0 ? visibleSetting.split(",") : [])
        const orderSetting = Plasmoid.configuration.profileOrder
        const order = orderSetting.length > 0 ? orderSetting.split(",") : []
        const positions = {}
        for (let index = 0; index < order.length; ++index)
            positions[order[index]] = index
        const profiles = []
        for (const profile of profileDirectory.profiles) {
            if (visible === null || visible.indexOf(profile.profileId) >= 0)
                profiles.push(profile)
        }
        profiles.sort((left, right) => {
            const leftPosition = positions[left.profileId] ?? Number.MAX_SAFE_INTEGER
            const rightPosition = positions[right.profileId] ?? Number.MAX_SAFE_INTEGER
            if (leftPosition !== rightPosition)
                return leftPosition - rightPosition
            return left.profileId.localeCompare(right.profileId)
        })
        return profiles
    }
    readonly property var hiddenProfiles: {
        if (!Plasmoid.configuration.hiddenProfilesAffectStatus)
            return []
        return profileDirectory.profiles.filter(profile =>
            !root.displayedProfiles.some(displayed => displayed.profileId === profile.profileId))
    }
    readonly property var primarySummary: {
        let selected = null
        const summaries = root.profileSummaries
        for (const profileId in summaries) {
            if (selected === null || summaries[profileId].priority < selected.priority)
                selected = summaries[profileId]
        }
        return selected
    }
    readonly property var attentionSummaries: BtrfsBackup.ProfilePresentation.sortedAttentionSummaries(root.profileSummaries)
    readonly property string attentionIcon: BtrfsBackup.ProfilePresentation.mostImportantAttention(root.profileSummaries)
    readonly property bool running: root.primarySummary?.running ?? false
    readonly property string compactEmblem: root.attentionIcon.length > 0
        ? root.attentionIcon
        : (root.running ? "emblem-synchronizing" : "")
    readonly property bool failed: root.primarySummary?.failed ?? false
    readonly property int progress: root.primarySummary?.progress ?? -1

    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: translations.i18n("Refresh status")
            icon.name: "view-refresh"
            enabled: profileDirectory.managerConnected
            onTriggered: {
                profileDirectory.refreshNow()
                root.refreshRevision++
            }
        },
        PlasmaCore.Action {
            text: translations.i18n("Manage backup profiles…")
            icon.name: "configure"
            onTriggered: profileDirectory.openSettings()
        }
    ]

    KI18n.KI18nContext {
        id: translations
        translationDomain: "plasma_applet_org.btrfsbackup.plasmoid"
    }

    Plasmoid.status: (root.primarySummary?.priority ?? 7) <= 3
        ? PlasmaCore.Types.ActiveStatus
        : PlasmaCore.Types.PassiveStatus
    Plasmoid.icon: "backup"
    toolTipMainText: translations.i18n("Btrfs Backups")
    toolTipSubText: root.tooltipText()

    BackupStatusModel {
        id: profileDirectory
        profile: "default"
        directoryOnly: true
        Component.onCompleted: start()
    }

    ProfileStatusStore {
        id: profileStatusStore
        directoryModel: profileDirectory
        profiles: profileDirectory.profiles
        historyLimitFor: profileId => root.displayedProfiles.some(profile => profile.profileId === profileId)
            ? Plasmoid.configuration.historyCount
            : 1
    }

    Timer {
        interval: 60000
        running: root.visible
        repeat: true
        onTriggered: root.relativeTimeTick++
    }

    function updateSummary(profileId, profileName, priority, isRunning, isFailed, profileProgress,
                           attentionPriority, attentionIcon, subtitle) {
        const summaries = Object.assign({}, root.profileSummaries)
        summaries[profileId] = {
            profileName: profileName,
            priority: priority,
            running: isRunning,
            failed: isFailed,
            progress: profileProgress,
            attentionPriority: attentionPriority,
            attentionIcon: attentionIcon,
            subtitle: subtitle
        }
        root.profileSummaries = summaries
    }

    function tooltipText() {
        if (root.attentionSummaries.length === 1) {
            const summary = root.attentionSummaries[0]
            return summary.profileName + " — " + summary.subtitle
        }
        if (root.attentionSummaries.length > 1) {
            const lines = [translations.i18np(
                "%1 profile requires attention",
                "%1 profiles require attention",
                root.attentionSummaries.length
            )]
            const shown = Math.min(3, root.attentionSummaries.length)
            for (let index = 0; index < shown; ++index) {
                const summary = root.attentionSummaries[index]
                lines.push(summary.profileName + " — " + summary.subtitle)
            }
            if (root.attentionSummaries.length > shown)
                lines.push(translations.i18n("and %1 more", root.attentionSummaries.length - shown))
            return lines.join("\n")
        }
        return root.primarySummary?.subtitle
            ?? profileDirectory.lastError
            ?? translations.i18n("No active backup")
    }

    function removeSummary(profileId) {
        const summaries = Object.assign({}, root.profileSummaries)
        delete summaries[profileId]
        root.profileSummaries = summaries
    }

    Item {
        visible: false

        Repeater {
            model: root.hiddenProfiles

            delegate: ProfileItem {
                required property var modelData

                visible: false
                profileId: modelData.profileId
                profileName: modelData.name
                targetNameHint: modelData.targetName
                statusProvidedExternally: true
                statusModelOverride: profileStatusStore.statusFor(profileId)
                relativeTimeTick: root.relativeTimeTick
                historyLimit: 1
                autoExpandActive: false
                autoExpandFailed: false
                showStorageDetails: false
                hideSourceNamesInTooltip: Plasmoid.configuration.hideSourceNamesInTooltip
                onSummaryUpdated: (profileId, profileName, priority, isRunning, isFailed, profileProgress,
                                   attentionPriority, attentionIcon, subtitle) =>
                    root.updateSummary(profileId, profileName, priority, isRunning, isFailed, profileProgress,
                                       attentionPriority, attentionIcon, subtitle)
                onSummaryRemoved: profileId => root.removeSummary(profileId)
            }
        }
    }

    compactRepresentation: MouseArea {
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        onClicked: root.expanded = !root.expanded

        Kirigami.Icon {
            anchors.fill: parent
            source: Plasmoid.icon
            opacity: profileDirectory.managerConnected ? 1 : 0.65
        }

        Kirigami.Icon {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: Math.max(6, Math.round(parent.width * 0.32))
            height: width
            source: root.compactEmblem
            visible: source.length > 0
        }

        Canvas {
            id: compactProgress
            anchors.fill: parent
            visible: root.running && root.progress >= 0
            onPaint: {
                const context = getContext("2d")
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
                target: root
                function onProgressChanged() { compactProgress.requestPaint() }
            }
        }

        Item {
            id: compactIndeterminateProgress
            anchors.fill: parent
            visible: root.running && root.progress < 0

            Canvas {
                anchors.fill: parent
                onPaint: {
                    const context = getContext("2d")
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

    }

    fullRepresentation: PlasmaExtras.Representation {
        implicitWidth: Kirigami.Units.gridUnit * 24
        implicitHeight: Kirigami.Units.gridUnit * 24
        Layout.minimumWidth: Kirigami.Units.gridUnit * 18
        Layout.minimumHeight: Kirigami.Units.gridUnit * 18
        Layout.maximumWidth: Kirigami.Units.gridUnit * 80
        Layout.maximumHeight: Kirigami.Units.gridUnit * 40
        focus: true
        collapseMarginsHint: true

        PlasmaComponents3.ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth - profilesView.leftMargin - profilesView.rightMargin
            PlasmaComponents3.ScrollBar.horizontal.policy: PlasmaComponents3.ScrollBar.AlwaysOff

            contentItem: ListView {
                id: profilesView

                model: root.displayedProfiles
                clip: true
                currentIndex: -1
                boundsBehavior: Flickable.StopAtBounds
                spacing: Kirigami.Units.smallSpacing
                leftMargin: 0
                rightMargin: 0
                topMargin: Kirigami.Units.smallSpacing
                bottomMargin: Kirigami.Units.smallSpacing
                cacheBuffer: 1000
                highlight: PlasmaExtras.Highlight {}
                highlightMoveDuration: Kirigami.Units.shortDuration
                highlightResizeDuration: Kirigami.Units.shortDuration

                delegate: ProfileItem {
                    required property var modelData

                    profileId: modelData.profileId
                    profileName: modelData.name
                    targetNameHint: modelData.targetName
                    statusProvidedExternally: true
                    statusModelOverride: profileStatusStore.statusFor(profileId)
                    relativeTimeTick: root.relativeTimeTick
                    historyLimit: Plasmoid.configuration.historyCount
                    autoExpandActive: Plasmoid.configuration.autoExpandActive
                    autoExpandFailed: Plasmoid.configuration.autoExpandFailed
                    showStorageDetails: Plasmoid.configuration.showStorage
                    hideSourceNamesInTooltip: Plasmoid.configuration.hideSourceNamesInTooltip
                    onSummaryUpdated: (profileId, profileName, priority, isRunning, isFailed, profileProgress,
                                       attentionPriority, attentionIcon, subtitle) =>
                        root.updateSummary(profileId, profileName, priority, isRunning, isFailed, profileProgress,
                                           attentionPriority, attentionIcon, subtitle)
                    onSummaryRemoved: profileId => root.removeSummary(profileId)
                }

                Loader {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 4
                    active: profilesView.count === 0
                    asynchronous: true
                    sourceComponent: PlasmaExtras.PlaceholderMessage {
                        width: parent.width
                        iconName: profileDirectory.managerConnected ? "drive-harddisk-symbolic" : "network-disconnect-symbolic"
                        text: profileDirectory.managerConnected
                            ? (profileDirectory.profiles.length > 0
                                ? translations.i18n("No profiles selected")
                                : translations.i18n("No backup profiles configured"))
                            : translations.i18n("Backup service unavailable")
                    }
                }
            }
        }
    }
}
