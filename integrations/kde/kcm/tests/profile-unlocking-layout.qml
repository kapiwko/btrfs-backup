// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtTest
import "../ui" as KcmUi

Item {
    width: 800
    height: 600

    QtObject {
        id: editor
        property string profileId: "default"
    }

    QtObject {
        id: credentials
        property var credentials: []
        property bool busy: false
        property string errorMessage: ""
        property string loadedProfileId: ""
        function load(profileId) { loadedProfileId = profileId }
        function clearError() {}
    }

    KcmUi.ProfileUnlocking {
        id: unlocking
        width: parent.width
        editor: editor
        profileId: "default"
        credentialModel: credentials
    }

    TestCase {
        name: "ProfileUnlockingLayout"
        when: windowShown

        function test_emptyMessageFitsInsideSection() {
            const message = findChild(unlocking, "emptyUnlockingMessage")
            verify(message !== null)
            verify(message.visible)
            verify(unlocking.implicitHeight >= message.y + message.implicitHeight)
            compare(credentials.loadedProfileId, "default")
        }
    }
}
