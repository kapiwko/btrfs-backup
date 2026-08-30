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
            Qt.exit(0)
        }
    }
}
