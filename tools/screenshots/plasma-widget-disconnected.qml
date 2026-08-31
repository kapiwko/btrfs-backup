// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

Item {
    Loader {
        Component.onCompleted: setSource("plasma-widget.qml", {
            "mode": "disconnected"
        })
    }
}
