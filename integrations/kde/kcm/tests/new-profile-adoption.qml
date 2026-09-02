// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import "../ui" as UI

Item {
    id: root
    width: 800
    height: 720

    readonly property var partition: ({
        kind: "existing-partition",
        candidateId: "partition-1",
        path: "/dev/test1",
        partitionLabel: "Backup",
        filesystemType: "crypto_LUKS",
        sectorCount: 1048576,
        mountPoints: [],
        blockers: [],
        suitableForAdoption: true,
        suitableForReformat: true,
        changed: false,
        dataWillBeErased: false
    })
    readonly property var device: ({
        candidateId: "device-1",
        path: "/dev/test",
        model: "Test disk",
        sizeBytes: 536870912,
        logicalSectorSize: 512,
        mounted: false,
        containsData: true,
        blockers: [],
        regions: [partition]
    })

    QtObject {
        id: editor
    }

    QtObject {
        id: provisioning
        property var devices: [root.device]
        property var topology: ({generation: "topology-1", devices: devices})
        property var inspection: ({
            inspectionId: "inspection-1",
            repositoryId: "repository-1",
            snapshotCount: 3
        })
        property var plan: ({
            planId: "plan-1",
            mode: "adopt-existing-target",
            displayPath: "/dev/test1",
            partitionId: "partition-1",
            before: {logicalSectorSize: 512, regions: [root.partition]},
            after: {logicalSectorSize: 512, regions: [root.partition]}
        })
        property var operation: ({})
        property var sourceCandidates: ["/home"]
        property bool busy: false
        property string errorMessage: ""
        signal completed(string profileId)
        function refresh() {}
        function buildPlan(selection, mode) {}
        function inspectExistingTarget(selection, passphrase) {}
        function clearSelection() {}
        function start(profileId, profileName, source, passphrase, confirmation, automaticKey) {}
        function poll() {}
        function cancel() {}
        function clearError() {}
    }

    UI.NewProfilePage {
        id: page
        anchors.fill: parent
        editor: editor
        provisioning: provisioning
        workflowMode: "adopt"
        step: 1
        selectedDevice: root.device
        selectedTarget: root.partition
    }

    Timer {
        interval: 100
        running: true
        repeat: false
        onTriggered: {
            if (!page.adoption || !page.hasPlan || page.selectedTarget.path !== "/dev/test1") {
                console.error("Existing target adoption page bindings are invalid")
                Qt.exit(1)
                return
            }
            Qt.exit(0)
        }
    }
}
