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

    BackupStatusModel {
        id: status
        profile: "default"
    }

    Timer {
        interval: 10
        running: true
        repeat: false
        onTriggered: {
            if (status.profile !== "default") {
                Qt.exit(2)
            }
            if (typeof status.command !== "undefined"
                    || typeof status.errorMessage !== "undefined") {
                Qt.exit(3)
            }
            if (typeof status.managerConnected !== "boolean"
                    || typeof status.canCancel !== "boolean"
                    || typeof status.safeToRemove !== "boolean"
                    || typeof status.targetConnected !== "boolean"
                    || typeof status.startBackup !== "function"
                    || typeof status.cancelBackup !== "function"
                    || typeof status.validateTarget !== "function"
                    || typeof status.ejectTarget !== "function"
                    || typeof status.watcherConnected !== "undefined") {
                Qt.exit(4)
            }
            Qt.exit(0)
        }
    }
}
