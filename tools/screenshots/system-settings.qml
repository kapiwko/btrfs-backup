// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import org.kde.kirigami as Kirigami

Window {
    id: window

    property string mode: "transferring"
    property string outputPath: "system-settings-transferring.png"
    readonly property bool previewMode: Qt.application.arguments.indexOf("--preview") >= 0

    width: 800
    height: 480
    visible: true
    title: "Btrfs Backup - System Settings"
    color: Kirigami.Theme.backgroundColor

    Component.onCompleted: requestActivate()

    QtObject {
        id: fakeDirectory

        property bool managerConnected: true
        property string lastError: ""
        property string lastErrorCode: ""
        property var profiles: [
            { profileId: "home", name: "Home backup", targetName: "Portable Backup" },
            { profileId: "archive", name: "Project archive", targetName: "Studio Archive" }
        ]

        function refreshNow() {}
        function openNotificationSettings() {}
    }

    QtObject {
        id: homeStatus

        property bool managerConnected: true
        property bool profileEnabled: true
        property bool operationPending: false
        property bool browseSupported: true
        property string lastOperation: ""
        property string lastError: ""
        property string lastErrorCode: ""
        property var run: ({
            state: window.mode === "transferring" ? "running"
                : window.mode === "connected" ? "succeeded" : "idle",
            lastSuccessAt: "2026-08-30 23:14",
            canCancel: window.mode === "transferring",
            activity: window.mode === "transferring" ? "transferring" : "idle",
            phase: window.mode === "transferring" ? "transfer" : "idle",
            sourceName: "Documents",
            targetName: "Portable Backup",
            overallProgress: window.mode === "transferring" ? 58 : 100,
            speedText: window.mode === "transferring" ? "45.5 MiB/s" : "",
            elapsedSeconds: window.mode === "transferring" ? 512 : 1440,
            etaSeconds: window.mode === "transferring" ? 371 : 0,
            errorCode: ""
        })
        property var target: ({
            name: "Portable Backup",
            connected: window.mode !== "disconnected",
            state: window.mode === "disconnected" ? "disconnected" : "mounted",
            storageKnown: window.mode !== "disconnected",
            availableText: window.mode === "disconnected" ? "" : "2.4 TiB",
            usedText: window.mode === "disconnected" ? "" : "1.6 TiB",
            usagePercent: 58,
            spaceBelowMinimum: false
        })

        function browseBackups() {}
        function startBackup() {}
        function cancelBackup() {}
        function ejectTarget() {}
        function setProfileEnabled(enabled) { profileEnabled = enabled }
        function validateTarget() {}
    }

    QtObject {
        id: archiveStatus

        property bool managerConnected: true
        property bool profileEnabled: false
        property bool operationPending: false
        property bool browseSupported: true
        property string lastOperation: ""
        property string lastError: ""
        property string lastErrorCode: ""
        property var run: ({
            state: "idle",
            lastSuccessAt: "2026-08-27 18:42",
            canCancel: false,
            activity: "idle",
            phase: "idle",
            sourceName: "Projects",
            targetName: "Studio Archive",
            overallProgress: -1,
            speedText: "",
            elapsedSeconds: -1,
            etaSeconds: -1,
            errorCode: ""
        })
        property var target: ({
            name: "Studio Archive",
            connected: window.mode === "connected",
            state: window.mode === "connected" ? "mounted" : "disconnected",
            storageKnown: window.mode === "connected",
            availableText: window.mode === "connected" ? "6.8 TiB" : "",
            usedText: window.mode === "connected" ? "1.2 TiB" : "",
            usagePercent: 92,
            spaceBelowMinimum: window.mode === "connected"
        })

        function browseBackups() {}
        function startBackup() {}
        function cancelBackup() {}
        function ejectTarget() {}
        function setProfileEnabled(enabled) { profileEnabled = enabled }
        function validateTarget() {}
    }

    QtObject {
        id: fakeEditor

        property bool loaded: true
        property bool busy: false
        property string profileId: "home"
        property string name: "Home backup"
        property string errorCode: ""
        property string errorMessage: ""
        property string operationMessage: ""
        property var target: ({})
        property var sources: []
        property var settings: ({})

        signal conflictDetected()
        signal profileChanged()
        signal stateChanged()
        signal profileSaved(string profileId)
        signal profileDeleted(string profileId)

        function load(id) { profileId = id }
        function loadDetails(id) { profileId = id }
        function addSourceConfiguration(name, subvolume, localRetention, remoteRetention) {}
        function updateSourceConfiguration(index, name, localRetention, remoteRetention) {}
        function removeSourceConfiguration(index) {}
        function deleteProfile() {}
        function updateProfileSettings(name, dailyLimit, autoEject) {
            editor.name = name
        }
        function reload() {}
    }

    QtObject {
        id: fakeHistory

        property string profileId: ""
        property bool loading: false
        property bool hasMore: true
        property string errorMessage: ""
        property var entries: [
            {
                state: "succeeded",
                errorCode: "",
                startedAt: "2026-08-30T20:50:00Z",
                finishedAt: "2026-08-30T21:14:00Z",
                durationSeconds: 1440,
                sourceCount: 2,
                bytesTransferred: 68719476736,
                bytesTransferredText: "64.0 GiB",
                averageSpeedText: "45.5 MiB/s"
            },
            {
                state: "failed",
                errorCode: "backup.failed",
                startedAt: "2026-08-29T20:45:00Z",
                finishedAt: "2026-08-29T20:52:18Z",
                durationSeconds: 438,
                sourceCount: 1,
                bytesTransferred: 12884901888,
                bytesTransferredText: "12.0 GiB",
                averageSpeedText: "28.1 MiB/s"
            }
        ]

        function loadFirstPage() {}
        function loadMore() { hasMore = false }
    }

    function saveScreenshot(path, completed) {
        window.contentItem.grabToImage(function(result) {
            if (!result.saveToFile(path)) {
                Qt.exit(2)
                return
            }
            completed()
        }, Qt.size(window.width, window.height))
    }

    Loader {
        id: kcmLoader

        anchors.fill: parent
        source: Qt.resolvedUrl("../../integrations/kde/kcm/ui/main.qml")
        onStatusChanged: {
            if (status === Loader.Error) {
                console.error("Failed to load the KCM screenshot scene")
                Qt.exit(2)
            }
        }
        onLoaded: {
            item.directoryOverride = fakeDirectory
            item.profileStatusOverrides = ({
                "home": homeStatus,
                "archive": archiveStatus
            })
            item.editorOverride = fakeEditor
            item.historyOverride = fakeHistory
        }
    }

    Timer {
        property int attempts: 0
        interval: 100
        running: !window.previewMode
        repeat: true
        onTriggered: {
            attempts++
            if (attempts === 20) {
                console.error("Timed out loading KCM screenshot scene; loader status:", kcmLoader.status)
                Qt.exit(2)
                return
            }
            if (kcmLoader.status !== Loader.Ready || !kcmLoader.item)
                return

            stop()
            Qt.callLater(function() {
                window.saveScreenshot(window.outputPath, function() {})
            })
        }
    }

    Timer {
        interval: 5000
        running: !window.previewMode
        repeat: false
        onTriggered: Qt.exit(0)
    }
}
