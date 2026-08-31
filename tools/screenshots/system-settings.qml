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
            lastSuccessAt: "2026-08-30 23:14"
        })
        property var target: ({
            name: "Portable Backup",
            connected: window.mode !== "disconnected",
            state: window.mode === "disconnected" ? "disconnected" : "mounted",
            storageKnown: window.mode !== "disconnected",
            availableText: window.mode === "disconnected" ? "" : "2.4 TiB"
        })

        function browseBackups() {}
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
            lastSuccessAt: "2026-08-27 18:42"
        })
        property var target: ({
            name: "Studio Archive",
            connected: window.mode === "connected",
            state: window.mode === "connected" ? "mounted" : "disconnected",
            storageKnown: window.mode === "connected",
            availableText: window.mode === "connected" ? "6.8 TiB" : ""
        })

        function browseBackups() {}
        function setProfileEnabled(enabled) { profileEnabled = enabled }
        function validateTarget() {}
    }

    QtObject {
        id: fakeEditor

        property bool loaded: true
        property bool newDraft: false
        property bool busy: false
        property bool dirty: false
        property string profileId: "home"
        property string name: "Home backup"
        property string errorCode: ""
        property string errorMessage: ""
        property string operationMessage: ""
        property string validationPreview: ""
        property var target: ({})
        property var sources: []
        property var settings: ({})

        signal conflictDetected()
        signal profileSaved(string profileId)
        signal profileDeleted(string profileId)

        function load(id) { profileId = id }
        function createDraft(id) { profileId = id }
        function setName(value) { name = value }
        function setTargetValue(key, value) {}
        function addSource() {}
        function removeSource(index) {}
        function setSourceValue(index, key, value) {}
        function setSettingValue(key, value) {}
        function validate() {}
        function duplicateAs(id) {}
        function deleteProfile() {}
        function discard() {}
        function save() {}
        function reload() {}
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
