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

        function test_errorReplacesEmptyMessage() {
            const message = findChild(unlocking, "emptyUnlockingMessage")
            credentials.errorMessage = "Device disconnected"
            wait(0)
            verify(message.visible)
            compare(message.text, "Device disconnected")
            verify(unlocking.implicitHeight >= message.y + message.implicitHeight)
            credentials.errorMessage = ""
        }

        function test_methodDetailsOpenFromActivation() {
            credentials.credentials = [{
                id: "slot-1",
                label: "Automatic backup key",
                type: "keyFile",
                keyslot: 1,
                managed: true,
                automatic: true
            }]
            wait(0)

            const row = findChild(unlocking, "unlockingMethodRow")
            const dialog = findChild(unlocking, "unlockingMethodDetailsDialog")
            verify(row !== null)
            verify(dialog !== null)

            row.clicked()
            wait(0)

            verify(dialog.visible)
            compare(dialog.title, "Automatic backup key")
            compare(findChild(dialog, "unlockingMethodTypeValue").text, "Key file")
            compare(findChild(dialog, "unlockingMethodSlotValue").text, "1")
            compare(findChild(dialog, "unlockingMethodManagementValue").text, "Managed by btrfs-backup")
            compare(findChild(dialog, "unlockingMethodUsageValue").text, "Automatic backups")

            credentials.credentials = []
            wait(0)

            verify(dialog.visible)
            compare(dialog.title, "Automatic backup key")
            compare(findChild(dialog, "unlockingMethodSlotValue").text, "1")

            credentials.credentials = [{
                id: "slot-2",
                label: "Replacement key",
                type: "passphrase",
                keyslot: 2,
                managed: false,
                automatic: false
            }]
            wait(0)

            verify(dialog.visible)
            compare(dialog.title, "Automatic backup key")
            compare(findChild(dialog, "unlockingMethodTypeValue").text, "Key file")
            compare(findChild(dialog, "unlockingMethodSlotValue").text, "1")

            dialog.close()
            tryCompare(dialog, "visible", false)

            const replacementRow = findChild(unlocking, "unlockingMethodRow")
            verify(replacementRow !== null)
            replacementRow.clicked()
            wait(0)
            compare(dialog.title, "LUKS key slot 2")
            compare(findChild(dialog, "unlockingMethodTypeValue").text, "Passphrase")
            dialog.close()
            tryCompare(dialog, "visible", false)
            credentials.credentials = []
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
            wait(0)

            const method = findChild(unlocking, "unlockingMethodDetails")
            verify(method !== null)
            compare(method.title, "LUKS key slot 0")
            verify(method.subtitle.startsWith("Unknown unlocking method"))

            credentials.credentials = []
        }
    }
}
