// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import org.btrfsbackup.kde

QtObject {
    id: root

    required property ProfileDirectoryModel directoryModel
    required property var profiles
    property var historyLimitFor: profileId => 1
    property var statusModels: ({})
    property int revision: 0

    readonly property Instantiator instances: Instantiator {
        model: root.profiles

        delegate: BackupStatusModel {
            required property var modelData

            profile: modelData.profileId
            directory: root.directoryModel
            historyLimit: root.historyLimitFor(profile)
            Component.onCompleted: start()
        }

        onObjectAdded: (index, object) => {
            const models = Object.assign({}, root.statusModels)
            models[object.profile] = object
            root.statusModels = models
            root.revision++
        }
        onObjectRemoved: (index, object) => {
            const models = Object.assign({}, root.statusModels)
            if (models[object.profile] === object)
                delete models[object.profile]
            root.statusModels = models
            root.revision++
        }
    }

    function statusFor(profileId) {
        root.revision
        return root.statusModels[profileId] ?? null
    }
}
