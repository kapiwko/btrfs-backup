// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Window 2.15
import org.btrfsbackup.kde 1.0
import "../package/contents/ui" as BackupUi

Window {
    id: root

    width: 1
    height: 1
    visible: false

    property int attempts: 0
    property bool initialStateObserved: false

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
        }
    }

    Timer {
        interval: 50
        running: true
        repeat: true
        onTriggered: {
            root.attempts++
            const profileItem = profiles.itemAtIndex(0) as BackupUi.ProfileItem
            if (!root.initialStateObserved
                    && status.managerConnected
                    && status.profileName === "Default backup"
                    && status.run.state === "running"
                    && status.run.runId === "20260829T160000Z-1-1"
                    && status.run.phase === "sizing"
                    && status.run.activity === "sizing"
                    && status.run.canCancel
                    && status.run.sourceName === "Home"
                    && status.run.targetName === "Backup disk"
                    && status.run.speedBps === 10
                    && status.run.etaSeconds === 20
                    && status.run.sourceProgress === 30
                    && status.run.overallProgress === 40
                    && status.run.progressAccuracy === "estimated"
                    && status.run.sourceIndex === 1
                    && status.run.sourceCount === 1
                    && status.run.startedAt === "2026-08-29T15:59:00Z"
                    && status.run.lastSuccessAt === "2026-08-24T18:42:00+0000"
                    && status.run.lastAttemptAt === "2026-08-25T10:00:00Z"
                    && status.run.lastAttemptState === "failed"
                    && status.target.connected
                    && status.target.safeToRemove
                    && status.target.state === "connected"
                    && status.history.entries.length === 1
                    && typeof status.startBackup === "function"
                    && typeof status.cancelBackup === "function"
                    && typeof status.validateTarget === "function"
                    && typeof status.ejectTarget === "function"
                    && profileItem !== null
                    && profileItem.profileId === "default"
                    && profileItem.running
                    && profileItem.progress === 40
                    && profileItem.profileStatus.history.entries.length === 1
                    && profileItem.defaultActionButtonVisible
                    && profileItem.defaultActionButtonAction.icon.name === "process-stop") {
                root.initialStateObserved = true
                console.warn("initial-manager-state-ready")
                return
            }
            if (root.initialStateObserved
                    && status.run.state === "succeeded"
                    && status.run.phase === "completed"
                    && !status.run.canCancel
                    && status.run.speedBps === 0
                    && status.run.sourceProgress === 100
                    && status.run.overallProgress === 100
                    && profileItem !== null
                    && !profileItem.running
                    && profileItem.progress === 100
                    && profileItem.defaultActionButtonVisible
                    && profileItem.defaultActionButtonAction.icon.name === "media-playback-start") {
                Qt.exit(0)
            }
            if (root.attempts === 50) {
                console.error("Initial manager state diagnostic:", status.managerConnected,
                              status.lastError, status.run.state, status.run.runId,
                              status.run.phase, status.run.activity, status.run.canCancel,
                              status.run.sourceName, status.run.targetName,
                              status.run.speedBps, status.run.etaSeconds,
                              status.run.sourceProgress, status.run.overallProgress,
                              status.run.progressAccuracy, status.target.connected,
                              status.target.safeToRemove, status.target.state,
                              status.history.entries.length, profileItem !== null,
                              profileItem !== null ? profileItem.profileId : "missing",
                              profileItem !== null ? profileItem.running : false,
                              profileItem !== null ? profileItem.progress : -2,
                              profileItem !== null ? profileItem.profileStatus.history.entries.length : -2,
                              profileItem !== null ? profileItem.defaultActionButtonVisible : false,
                              profileItem !== null ? profileItem.defaultActionButtonAction.icon.name : "missing")
            }
            if (root.attempts >= 200) {
                console.error("D-Bus backend did not react to the manager signal:", status.lastError)
                Qt.exit(2)
            }
        }
    }
}
