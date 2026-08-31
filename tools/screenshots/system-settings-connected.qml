// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

Item {
    Loader {
        Component.onCompleted: setSource("system-settings.qml", {
            "mode": "connected",
            "outputPath": "system-settings-connected.png"
        })
    }
}
