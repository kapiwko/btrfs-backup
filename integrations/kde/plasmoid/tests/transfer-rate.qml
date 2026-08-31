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

    BackupUi.TransferRate {
        id: rate
        anchors.fill: parent
        active: true
        currentSpeed: 2048
        currentSpeedText: "2,0 KiB/s"
    }

    Timer {
        interval: 100
        running: true
        repeat: false
        onTriggered: {
            rate.currentSpeed = 4096
            rate.currentSpeedText = "4,0 KiB/s"
            if (rate.peakSpeed !== 4096
                    || rate.peakSpeedText !== "4,0 KiB/s")
                Qt.exit(2)
            Qt.exit(0)
        }
    }
}
