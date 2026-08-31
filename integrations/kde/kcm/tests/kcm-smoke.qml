// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

Item {
    width: 800
    height: 640

    Loader {
        id: page
        anchors.fill: parent
        source: "../ui/main.qml"
    }

    QtObject {
        id: editor
        property string profileId: "default"
        property string name: "Default backup"
        property bool enabled: true
        property var target: ({device: "/dev/test", luksUuid: "uuid", btrfsUuid: "uuid", mapperName: "backup", activation: {mode: "askPassword"}})
        property var settings: ({remoteRetention: 30, localRetention: 30})
        property var sources: [{name: "Home", subvolume: "/home", localSnapshotDir: "/.snapshots/home", remoteSubdir: "home"}]
        property var sourceCandidates: ["/home", "/srv/work"]
        property bool configurationValid: true
        property string configurationErrorCode: ""
        property bool loaded: true
        property bool busy: false
        property string errorCode: ""
        property string errorMessage: ""
        property string operationMessage: ""
        signal profileChanged()
        signal conflictDetected()
        signal stateChanged()
        signal profileSaved(string profileId)
        signal profileDeleted(string profileId)
        function load(profileId) {}
        function loadDetails(profileId) {}
        function reload() {}
        function addSourceConfiguration(name, subvolume, localRetention, remoteRetention) {}
        function updateSourceConfiguration(index, name, localRetention, remoteRetention) {}
        function removeSourceConfiguration(index) {}
        function updateProfileSettings(name, dailyLimit, autoEject) {}
        function deleteProfile() {}
    }

    Timer {
        interval: 100
        running: true
        repeat: false
        onTriggered: {
            if (page.status !== Loader.Ready || page.item === null) {
                console.error("Backup KCM page did not load", page.status)
                Qt.exit(1)
                return
            }
            page.item.editorOverride = editor
            page.item.openEditorFor("default")
            verifyDialog.start()
        }
    }

    Timer {
        id: verifyDialog
        interval: 100
        onTriggered: Qt.exit(0)
    }
}
