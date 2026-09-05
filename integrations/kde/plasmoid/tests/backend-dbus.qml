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
        directoryOnly: true
        Component.onCompleted: start()
    }

    BackupUi.ProfileStatusStore {
        id: statusStore
        directoryModel: status
        profiles: status.profiles
        historyLimitFor: profileId => 3
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
            statusProvidedExternally: true
            statusModelOverride: statusStore.statusFor(profileId)
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
                    && profileItem !== null
                    && profileItem.profileStatus === statusStore.statusFor("default")
                    && profileItem.profileStatus.run.state === "running"
                    && profileItem.profileStatus.run.runId === "20260829T160000Z-1-1"
                    && profileItem.profileStatus.run.phase === "sizing"
                    && profileItem.profileStatus.run.activity === "sizing"
                    && profileItem.profileStatus.run.canCancel
                    && profileItem.profileStatus.run.sourceName === "Home"
                    && profileItem.profileStatus.run.targetName === "Backup disk"
                    && profileItem.profileStatus.run.speedBps === 10
                    && profileItem.profileStatus.run.etaSeconds === 20
                    && profileItem.profileStatus.run.sourceProgress === 30
                    && profileItem.profileStatus.run.overallProgress === 40
                    && profileItem.profileStatus.run.progressAccuracy === "estimated"
                    && profileItem.profileStatus.run.sourceIndex === 1
                    && profileItem.profileStatus.run.sourceCount === 1
                    && profileItem.profileStatus.run.startedAt === "2026-08-29T15:59:00Z"
                    && profileItem.profileStatus.run.lastSuccessAt === "2026-08-24T18:42:00+0000"
                    && profileItem.profileStatus.run.lastAttemptAt === "2026-08-25T10:00:00Z"
                    && profileItem.profileStatus.run.lastAttemptState === "failed"
                    && profileItem.profileStatus.target.connected
                    && profileItem.profileStatus.target.safeToRemove
                    && profileItem.profileStatus.target.state === "connected"
                    && profileItem.profileStatus.history.entries.length === 1
                    && typeof status.startBackup === "function"
                    && typeof status.cancelBackup === "function"
                    && typeof status.validateTarget === "function"
                    && typeof status.ejectTarget === "function"
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
                    && profileItem.profileStatus.run.state === "succeeded"
                    && profileItem.profileStatus.run.phase === "completed"
                    && !profileItem.profileStatus.run.canCancel
                    && profileItem.profileStatus.run.speedBps === 0
                    && profileItem.profileStatus.run.sourceProgress === 100
                    && profileItem.profileStatus.run.overallProgress === 100
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
