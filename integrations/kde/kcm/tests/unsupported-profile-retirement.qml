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
        id: target
        property bool connected: false
        property bool storageKnown: false
        property bool spaceBelowMinimum: false
        property real usagePercent: 0
        property string availableText: ""
        property string state: "unknown"
    }

    QtObject {
        id: run
        property string state: "unknown"
    }

    QtObject {
        id: status
        property var target: target
        property var run: run
        property bool operationPending: false
        property bool browseSupported: false
        property bool profileEnabled: false
        property bool configurationValid: false
        property string configurationErrorCode: "configuration.unsupported-schema"
        property int detectedSchemaVersion: 4
        property int supportedSchemaVersion: 1
    }

    QtObject {
        id: editor
        property bool busy: false
        property string retiredProfileId: ""
        function retireUnsupportedProfile(profileId) {
            retiredProfileId = profileId
        }
    }

    QtObject { id: directory }

    KcmUi.ProfileDelegate {
        id: delegate
        width: parent.width
        index: 0
        modelData: ({profileId: "legacy", name: "Legacy profile"})
        directory: directory
        editor: editor
        statusOverride: status
        profileSummaryFor: (profileStatus, profile) => "Unsupported configuration"
    }

    TestCase {
        name: "UnsupportedProfileRetirement"
        when: windowShown

        function test_retirementRequiresConfirmation() {
            const automaticBackupsSwitch = findChild(delegate, "automaticBackupsSwitch")
            const action = findChild(delegate, "retireUnsupportedProfileAction")
            const dialog = findChild(delegate, "retireUnsupportedProfileDialog")
            verify(automaticBackupsSwitch !== null)
            verify(action !== null)
            verify(dialog !== null)
            compare(automaticBackupsSwitch.enabled, false)
            compare(action.visible, true)
            compare(action.enabled, true)

            action.trigger()
            tryCompare(dialog, "visible", true)
            compare(editor.retiredProfileId, "")
            dialog.accept()
            tryCompare(editor, "retiredProfileId", "legacy")
        }
    }
}
