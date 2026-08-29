// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Window 2.15
import "../package/contents/ui" as BackupUi

Window {
    width: 320
    height: 180
    visible: false

    BackupUi.TransferSpeedChart {
        id: chart
        anchors.fill: parent
        active: true
        currentSpeed: 2048
    }

    Timer {
        interval: 2200
        running: true
        repeat: false
        onTriggered: {
            if (chart.samples.length < 2 || chart.peakSpeed !== 2048)
                Qt.exit(2)
            Qt.exit(0)
        }
    }
}
