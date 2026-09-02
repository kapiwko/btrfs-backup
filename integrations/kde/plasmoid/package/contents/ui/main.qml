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
import org.btrfsbackup.plasma

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
    readonly property var primarySummary: {
        let selected = null
        const summaries = root.profileSummaries
        for (const profileId in summaries) {
            if (selected === null || summaries[profileId].priority < selected.priority)
                selected = summaries[profileId]
        }
        return selected
    }
    readonly property bool running: root.primarySummary?.running ?? false
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
    toolTipMainText: translations.i18n("Btrfs Backups")
    toolTipSubText: root.primarySummary?.subtitle
        ?? profileDirectory.lastError
        ?? translations.i18n("No active backup")

    BackupStatusModel {
        id: profileDirectory
        profile: "default"
        Component.onCompleted: start()
    }

    Timer {
        interval: 60000
        running: root.visible
        repeat: true
        onTriggered: root.relativeTimeTick++
    }

    function updateSummary(profileId, priority, isRunning, isFailed, profileProgress, subtitle) {
        const summaries = Object.assign({}, root.profileSummaries)
        summaries[profileId] = {
            priority: priority,
            running: isRunning,
            failed: isFailed,
            progress: profileProgress,
            subtitle: subtitle
        }
        root.profileSummaries = summaries
    }

    function removeSummary(profileId) {
        const summaries = Object.assign({}, root.profileSummaries)
        delete summaries[profileId]
        root.profileSummaries = summaries
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
            source: root.panelIcon()
            opacity: profileDirectory.managerConnected ? 1 : 0.65
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

    function panelIcon() {
        switch (root.primarySummary?.priority ?? 7) {
        case 1: return "dialog-error-symbolic"
        case 3: return "dialog-warning-symbolic"
        case 5: return "media-eject-symbolic"
        case 6: return "emblem-success-symbolic"
        default: return "drive-harddisk-symbolic"
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
                    relativeTimeTick: root.relativeTimeTick
                    refreshRevision: root.refreshRevision
                    historyLimit: Plasmoid.configuration.historyCount
                    autoExpandActive: Plasmoid.configuration.autoExpandActive
                    autoExpandFailed: Plasmoid.configuration.autoExpandFailed
                    showStorageDetails: Plasmoid.configuration.showStorage
                    hideSourceNamesInTooltip: Plasmoid.configuration.hideSourceNamesInTooltip
                    onSummaryUpdated: (profileId, priority, isRunning, isFailed, profileProgress, subtitle) =>
                        root.updateSummary(profileId, priority, isRunning, isFailed, profileProgress, subtitle)
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
