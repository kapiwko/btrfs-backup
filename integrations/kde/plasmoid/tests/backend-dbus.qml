// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Window 2.15
import org.btrfsbackup.plasma 1.0
import "../package/contents/ui" as BackupUi

Window {
    id: root

    width: 1
    height: 1
    visible: false

    property int attempts: 0

    BackupStatusModel {
        id: status
        profile: "default"
        Component.onCompleted: start()
    }

    ListView {
        id: profiles
        width: 400
        height: 400
        model: [{
            profileId: "default",
            name: "Default backup",
            targetName: "Backup disk"
        }]

        delegate: BackupUi.ProfileItem {
            required property var modelData

            profileId: modelData.profileId
            profileName: modelData.name
            targetNameHint: modelData.targetName
            relativeTimeTick: 0
            refreshRevision: 0
        }
    }

    Timer {
        interval: 50
        running: true
        repeat: true
        onTriggered: {
            root.attempts++
            const profileItem = profiles.itemAtIndex(0) as BackupUi.ProfileItem
            if (status.managerConnected
                    && status.profileName === "Default backup"
                    && status.state === "running"
                    && status.runId === "20260829T160000Z-1-1"
                    && status.phase === "sizing"
                    && status.activity === "sizing"
                    && status.canCancel
                    && status.currentSourceName === "Home"
                    && status.targetName === "Backup disk"
                    && status.speedBps === 10
                    && status.etaSeconds === 20
                    && status.sourceProgress === 30
                    && status.overallProgress === 40
                    && status.progressAccuracy === "estimated"
                    && status.targetConnected
                    && status.safeToRemove
                    && status.targetState === "connected"
                    && status.history.length === 1
                    && typeof status.startBackup === "function"
                    && typeof status.cancelBackup === "function"
                    && typeof status.validateTarget === "function"
                    && typeof status.ejectTarget === "function"
                    && profileItem !== null
                    && profileItem.profileId === "default"
                    && profileItem.running
                    && profileItem.progress === 40
                    && profileItem.historyCount === 1
                    && profileItem.defaultActionButtonVisible
                    && profileItem.defaultActionButtonAction.icon.name === "process-stop") {
                Qt.exit(0)
            }
            if (root.attempts >= 100) {
                console.error("D-Bus backend did not expose the expected manager state:", status.lastError)
                Qt.exit(2)
            }
        }
    }
}
