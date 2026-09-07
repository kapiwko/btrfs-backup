// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import org.btrfsbackup.kde 1.0

Item {
    property int attempts: 0

    ProfileDirectoryModel {
        id: directory
        Component.onCompleted: start()
    }

    BackupStatusModel {
        id: status
        profile: "default"
        directory: directory
        Component.onCompleted: start()
    }

    Timer {
        interval: 50
        running: true
        repeat: true
        onTriggered: {
            attempts++
            if (status.managerConnected
                    && status.configurationErrorCode === "configuration.unsupported-schema"
                    && status.history.profileId === ""
                    && status.history.entries.length === 0) {
                Qt.exit(0)
            }
            if (attempts >= 100) {
                console.error("Unsupported profile history was requested:",
                              status.configurationErrorCode,
                              status.history.profileId,
                              status.history.entries.length,
                              status.lastErrorCode)
                Qt.exit(2)
            }
        }
    }
}
