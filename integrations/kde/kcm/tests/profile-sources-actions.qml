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
            localRetention: 30,
            remoteRetention: 30
        }]
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

        function test_clickingSourceOpensEditor() {
            const row = findChild(sources, "sourceRow")
            const dialog = findChild(sources, "sourceDialog")
            verify(row !== null)
            verify(dialog !== null)

            mouseClick(row, 10, row.height / 2)

            tryCompare(dialog, "visible", true)
            compare(dialog.editing, true)
            dialog.close()
        }
    }
}
