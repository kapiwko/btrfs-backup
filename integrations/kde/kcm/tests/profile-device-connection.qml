// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtTest
import "../ui" as KcmUi

Item {
    width: 800
    height: 600

    QtObject {
        id: target
        property bool connected: true
        property string name: "backupdisk"
        property string state: "unlocked"
        property bool storageKnown: false
        property real usagePercent: 0
        property string usedText: ""
        property string availableText: ""
    }

    QtObject {
        id: run
        property string state: "success"
        property string targetName: ""
        property string lastSuccessAt: ""
        property string errorCode: ""
    }

    QtObject {
        id: status
        property var target: target
        property var run: run
        property bool profileEnabled: false
        property string lastError: ""
    }

    KcmUi.ProfileDetails {
        id: details
        width: parent.width
        profileStatus: status
        targetNameHint: ""
        statusTextFor: state => state
        targetStateTextFor: state => state
    }

    TestCase {
        name: "ProfileDeviceConnection"
        when: windowShown

        function test_connectionIsIndependentOfTargetState() {
            const connection = findChild(details, "deviceConnectionState")
            verify(connection !== null)
            compare(connection.text, "Connected")
            compare(target.state, "unlocked")

            target.connected = false
            wait(0)
            compare(connection.text, "Disconnected")
            compare(target.state, "unlocked")
        }
    }
}
