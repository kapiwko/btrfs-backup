// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtTest
import "../ui" as KcmUi

Item {
    id: root

    width: 800
    height: 600

    QtObject {
        id: editor

        property bool busy: false
        property var sources: [{
            id: "home",
            name: "Home",
            subvolume: "/home",
            localSnapshotDir: "/.snapshots/home",
            remoteSubdir: "home",
            localRetention: 30,
            remoteRetention: 30
        }]
        property var sourceCandidates: ["/home", "/srv/work"]
    }

    KcmUi.ProfileSources {
        id: sources

        anchors.fill: parent
        editor: editor
    }

    TestCase {
        name: "ProfileSourcesActions"
        when: windowShown

        function test_addOpensDialog() {
            const action = findChild(sources, "addSourceAction")
            const dialog = findChild(sources, "sourceDialog")
            verify(action !== null)
            verify(dialog !== null)

            action.trigger()

            tryCompare(dialog, "visible", true)
            dialog.close()
            tryCompare(dialog, "visible", false)
        }

        function test_clickingSourceOpensDetails() {
            const row = findChild(sources, "sourceRow")
            const dialog = findChild(sources, "sourceDetailsDialog")
            verify(row !== null)
            verify(dialog !== null)

            mouseClick(row, 10, row.height / 2)

            tryCompare(dialog, "visible", true)
            compare(dialog.title, "Home")
            compare(findChild(dialog, "sourceDetailsSubvolume").text, "/home")
            compare(findChild(dialog, "sourceDetailsLocalDirectory").text, "/.snapshots/home")
            compare(findChild(dialog, "sourceDetailsTargetDirectory").text, "home")
            compare(findChild(dialog, "sourceDetailsLocalRetention").text, "30")
            compare(findChild(dialog, "sourceDetailsTargetRetention").text, "30")
            dialog.close()
            tryCompare(dialog, "visible", false)
        }

        function test_editActionOpensEditor() {
            const row = findChild(sources, "sourceRow")
            const dialog = findChild(sources, "sourceDialog")
            const details = findChild(sources, "sourceDetailsDialog")
            const subvolume = findChild(sources, "subvolumeField")
            verify(row !== null)
            const action = findChild(row, "editSourceAction")
            verify(action !== null)
            verify(dialog !== null)
            verify(details !== null)
            verify(subvolume !== null)

            action.trigger()

            tryCompare(dialog, "visible", true)
            compare(details.visible, false)
            compare(dialog.editing, true)
            compare(subvolume.currentText, "/home")
            dialog.close()
            tryCompare(dialog, "visible", false)
        }
    }
}
