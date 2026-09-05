// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

Item {
    width: 800
    height: 640

    QtObject {
        id: settings
        property bool enabled: true
        property int warningDays: 7
        property int criticalDays: 14
        property bool storageEnabled: true
        property int storageWarningPercent: 15
        property int storageCriticalPercent: 5
    }

    Loader {
        id: page
        anchors.fill: parent
        Component.onCompleted: setSource("../ui/NotificationSettingsPage.qml", {"settings": settings})
    }

    Timer {
        interval: 100
        running: true
        onTriggered: {
            if (page.status !== Loader.Ready || page.item === null) {
                console.error("Notification settings page did not load", page.status)
                Qt.exit(1)
                return
            }
            if (page.item.settings.storageWarningPercent !== 15
                    || page.item.settings.storageCriticalPercent !== 5) {
                console.error("Notification thresholds were not exposed")
                Qt.exit(1)
                return
            }
            Qt.exit(0)
        }
    }
}
