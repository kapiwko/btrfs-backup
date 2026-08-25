// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Window 2.15
import org.btrfsbackup.plasma 1.0

Window {
    width: 1
    height: 1
    visible: false

    property int attempts: 0

    BackupStatusModel {
        id: status
        profile: "default"
        Component.onCompleted: start()
    }

    Timer {
        interval: 50
        running: true
        repeat: true
        onTriggered: {
            attempts++
            if (status.managerConnected
                    && status.profileName === "Default backup"
                    && status.state === "running"
                    && status.currentSourceName === "Home"
                    && status.targetName === "Backup disk"
                    && status.speedBps === 10
                    && status.etaSeconds === 20
                    && status.sourceProgress === 30
                    && status.overallProgress === 40
                    && status.progressAccuracy === "estimated") {
                Qt.exit(0)
            }
            if (attempts >= 100) {
                console.error("D-Bus backend did not expose the expected manager state:", status.lastError)
                Qt.exit(2)
            }
        }
    }
}
