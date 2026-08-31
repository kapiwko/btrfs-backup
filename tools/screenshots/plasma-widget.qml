// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras
import "../../integrations/kde/plasmoid/package/contents/ui" as BackupUi

Window {
    id: window

    property string mode: "transferring"
    property bool expandPrimary: true

    width: 500
    height: 500
    visible: true
    title: "Btrfs Backups"
    color: Kirigami.Theme.backgroundColor

    Component.onCompleted: requestActivate()

    QtObject {
        id: fakeStatus

        property bool managerConnected: true
        property bool profileEnabled: true
        property bool operationPending: false
        property bool browseSupported: true
        property string lastError: ""
        property string lastOperation: ""
        property var run: window.mode === "transferring"
            ? window.runState({
                state: "running",
                overallProgress: 68,
                canCancel: true,
                activity: "transferring",
                phase: "transferring",
                speedBps: 94371840,
                speedText: "90.0 MiB/s",
                sourceName: "Documents",
                sourceIndex: 2,
                etaSeconds: 754,
                elapsedSeconds: 1126
            })
            : window.runState({ state: "idle" })
        property var target: window.mode === "disconnected"
            ? window.targetState({
                connected: false,
                mounted: false,
                unlocked: false,
                safeToRemove: false,
                state: "disconnected",
                storageLive: false,
                storageMeasuredAt: "2026-08-30T21:14:00Z"
            })
            : window.targetState({ safeToRemove: window.mode !== "transferring" })
        property var history: ({
            entries: [
                { state: "succeeded", durationSeconds: 1432, sourceCount: 3,
                  errorCode: "", finishedAt: "2026-08-30T21:14:00Z" },
                { state: "succeeded", durationSeconds: 1389, sourceCount: 3,
                  errorCode: "", finishedAt: "2026-08-29T21:08:00Z" }
            ]
        })

        signal statusChanged()

        function startBackup() {}
        function cancelBackup() {}
        function ejectTarget() {}
        function setProfileEnabled(enabled) { profileEnabled = enabled }
        function browseBackups() {}
        function refreshNow() {}
    }

    QtObject {
        id: fakeArchiveStatus

        property bool managerConnected: true
        property bool profileEnabled: false
        property bool operationPending: false
        property bool browseSupported: true
        property string lastError: ""
        property string lastOperation: ""
        property var run: window.runState({
            state: "idle",
            targetName: "Studio Archive",
            lastSuccessAt: "2026-08-27T16:42:00Z"
        })
        property var target: window.mode === "connected"
            ? window.targetState({
                name: "Studio Archive",
                capacityText: "8.0 TiB",
                usedText: "1.2 TiB",
                availableText: "6.8 TiB",
                usagePercent: 15
            })
            : window.targetState({
                connected: false,
                mounted: false,
                unlocked: false,
                safeToRemove: false,
                name: "Studio Archive",
                state: "disconnected",
                storageKnown: false,
                storageLive: false
            })
        property var history: ({ entries: [] })

        signal statusChanged()

        function startBackup() {}
        function cancelBackup() {}
        function ejectTarget() {}
        function setProfileEnabled(enabled) { profileEnabled = enabled }
        function browseBackups() {}
        function refreshNow() {}
    }

    function runState(overrides) {
        return Object.assign({
            state: "idle",
            overallProgress: -1,
            canCancel: false,
            activity: "",
            phase: "",
            speedBps: 0,
            speedText: "0 B/s",
            targetName: "Portable Backup",
            lastSuccessAt: "2026-08-30T21:14:00Z",
            lastAttemptState: "succeeded",
            lastAttemptAt: "2026-08-30T21:14:00Z",
            sourceName: "",
            sourceIndex: 0,
            sourceCount: 3,
            etaSeconds: -1,
            elapsedSeconds: 0,
            errorCode: ""
        }, overrides)
    }

    function targetState(overrides) {
        return Object.assign({
            connected: true,
            mounted: true,
            unlocked: true,
            safeToRemove: true,
            name: "Portable Backup",
            state: "mounted",
            storageSupported: true,
            storageKnown: true,
            capacityText: "3.6 TiB",
            usedText: "1.2 TiB",
            availableText: "2.4 TiB",
            usagePercent: 33,
            storageLive: true,
            storageMeasuredAt: "2026-08-31T10:32:00Z",
            spaceBelowMinimum: false
        }, overrides)
    }

    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
    }

    PlasmaExtras.Representation {
        id: capture

        anchors.fill: parent

        background: KSvg.FrameSvgItem {
            imagePath: "dialogs/background"
        }

        header: PlasmaExtras.PlasmoidHeading {
            topPadding: Kirigami.Units.smallSpacing
            leftPadding: Kirigami.Units.smallSpacing
            rightPadding: Kirigami.Units.smallSpacing

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                PlasmaComponents3.ToolButton {
                    icon.name: "go-previous-symbolic"
                }

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 2
                    text: "Btrfs Backups"
                    elide: Text.ElideRight
                }

                PlasmaComponents3.ToolButton {
                    icon.name: "view-refresh"
                }

                PlasmaComponents3.ToolButton {
                    icon.name: "configure"
                }

                PlasmaComponents3.ToolButton {
                    icon.name: "window-pin"
                    checkable: true
                    checked: true
                }
            }
        }

        contentItem: PlasmaComponents3.ScrollView {
            id: profilesScrollView

            contentWidth: Math.max(0, availableWidth - profilesView.leftMargin - profilesView.rightMargin)
            PlasmaComponents3.ScrollBar.horizontal.policy: PlasmaComponents3.ScrollBar.AlwaysOff

            contentItem: ListView {
                id: profilesView

                model: [
                    { profileId: "home", name: "Home backup", targetName: "Portable Backup" },
                    { profileId: "archive", name: "Project archive", targetName: "Studio Archive" }
                ]
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                leftMargin: 0
                rightMargin: 0
                topMargin: Kirigami.Units.smallSpacing
                bottomMargin: Kirigami.Units.smallSpacing

                delegate: BackupUi.ProfileItem {
                    id: profileItem

                    required property var modelData

                    width: profilesScrollView.contentWidth
                    profileId: modelData.profileId
                    profileName: modelData.name
                    targetNameHint: modelData.targetName
                    relativeTimeTick: 0
                    historyLimit: 3
                    showStorageDetails: true
                    statusModelOverride: profileId === "home" ? fakeStatus : fakeArchiveStatus
                    Component.onCompleted: {
                        if (window.expandPrimary && index === 0)
                            expand()
                    }
                }
            }
        }
    }

    Timer {
        property int sampleIndex: 0
        property var sampleSpeeds: [73400320, 99614720, 84934656, 113246208]
        interval: 650
        running: fakeStatus.run.state === "running"
        repeat: true
        onTriggered: {
            const speed = sampleSpeeds[sampleIndex % sampleSpeeds.length]
            const nextRun = Object.assign({}, fakeStatus.run)
            nextRun.speedBps = speed
            nextRun.speedText = (speed / 1048576).toFixed(1) + " MiB/s"
            fakeStatus.run = nextRun
            sampleIndex++
        }
    }

}
