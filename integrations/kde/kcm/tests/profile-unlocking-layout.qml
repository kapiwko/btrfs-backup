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
        height: parent.height
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

        function test_errorReplacesEmptyMessage() {
            const message = findChild(unlocking, "emptyUnlockingMessage")
            credentials.errorMessage = "Device disconnected"
            wait(0)
            verify(message.visible)
            compare(message.text, "Device disconnected")
            verify(unlocking.implicitHeight >= message.y + message.implicitHeight)
            credentials.errorMessage = ""
        }

        function test_methodRowsFollowCredentialRefresh() {
            credentials.credentials = [{
                id: "slot-1",
                label: "Automatic backup key",
                type: "keyFile",
                keyslot: 1,
                managed: true,
                automatic: true
            }]
            tryCompare(unlocking, "credentialCount", 1)
            tryVerify(() => findChild(unlocking, "unlockingMethodRow") !== null)

            let row = findChild(unlocking, "unlockingMethodRow")
            verify(row !== null)
            compare(row.credentialId, "slot-1")
            compare(findChild(row, "unlockingMethodDetails").title, "Automatic backup key")
            verify(findChild(row, "unlockingMethodDetails").subtitle.includes("Key file"))

            credentials.credentials = []
            tryCompare(unlocking, "credentialCount", 0)
            tryVerify(() => findChild(unlocking, "unlockingMethodRow") === null)

            credentials.credentials = [{
                id: "slot-2",
                label: "Replacement key",
                type: "passphrase",
                keyslot: 2,
                managed: false,
                automatic: false
            }]
            tryCompare(unlocking, "credentialCount", 1)
            tryVerify(() => {
                const currentRow = findChild(unlocking, "unlockingMethodRow")
                return currentRow !== null && currentRow.credentialId === "slot-2"
            })

            row = findChild(unlocking, "unlockingMethodRow")
            compare(findChild(row, "unlockingMethodDetails").title, "LUKS key slot 2")
            verify(findChild(row, "unlockingMethodDetails").subtitle.includes("Passphrase"))
            credentials.credentials = []
            tryCompare(unlocking, "credentialCount", 0)
            tryVerify(() => findChild(unlocking, "unlockingMethodRow") === null)
        }

        function test_busyOperationIsVisible() {
            const message = findChild(unlocking, "emptyUnlockingMessage")
            const progress = findChild(unlocking, "unlockingMethodsProgress")
            const indicator = findChild(unlocking, "unlockingMethodsBusyIndicator")
            const progressText = findChild(unlocking, "unlockingMethodsProgressText")

            credentials.busy = true
            wait(0)

            verify(progress.visible)
            verify(indicator.running)
            compare(progressText.text, "Updating unlocking methods…")
            verify(!message.visible)

            credentials.busy = false
        }

        function test_unknownLuksSlotRemainsVisible() {
            credentials.credentials = [{
                id: "slot-0",
                label: "Other credential",
                type: "unknown",
                keyslot: 0,
                managed: false,
                automatic: false
            }]
            tryCompare(unlocking, "credentialCount", 1)
            tryVerify(() => findChild(unlocking, "unlockingMethodDetails") !== null)

            const method = findChild(unlocking, "unlockingMethodDetails")
            verify(method !== null)
            compare(method.title, "LUKS key slot 0")
            verify(method.subtitle.startsWith("Unknown unlocking method"))

            credentials.credentials = []
            tryCompare(unlocking, "credentialCount", 0)
            tryVerify(() => findChild(unlocking, "unlockingMethodRow") === null)
        }
    }
}
