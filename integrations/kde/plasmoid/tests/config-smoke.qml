// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

Item {
    width: 640
    height: 720

    Loader {
        id: page
        anchors.fill: parent
        source: "../package/contents/ui/configGeneral.qml"
    }

    Timer {
        interval: 100
        running: true
        repeat: false
        onTriggered: {
            if (page.status !== Loader.Ready || page.item === null) {
                console.error("Plasmoid configuration page did not load", page.status)
                Qt.exit(1)
                return
            }
            if (typeof page.item.cfg_visibleProfiles !== "string"
                    || typeof page.item.cfg_profileOrder !== "string"
                    || typeof page.item.cfg_historyCount !== "number"
                    || typeof page.item.cfg_autoExpandActive !== "boolean"
                    || typeof page.item.cfg_autoExpandFailed !== "boolean"
                    || typeof page.item.cfg_showStorage !== "boolean"
                    || typeof page.item.cfg_hideSourceNamesInTooltip !== "boolean") {
                console.error("Plasmoid configuration properties are incomplete")
                Qt.exit(2)
                return
            }
            Qt.exit(0)
        }
    }
}
