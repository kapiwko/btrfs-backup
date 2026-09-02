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
        filesystemType: "ext4",
        sectorCount: 524288,
        mountPoints: [],
        blockers: [],
        suitableForReformat: true,
        changed: false,
        dataWillBeErased: false
    })
    readonly property var freeRegion: ({
        kind: "unallocated",
        candidateId: "free-1",
        startSector: 526336,
        sectorCount: 522240,
        blockers: [],
        suitableForBackupPartition: true,
        changed: false,
        dataWillBeErased: false
    })
    readonly property var plannedPartition: ({
        kind: "backup-partition",
        candidateId: "planned-backup-partition",
        startSector: 526336,
        sectorCount: 522240,
        partitionNumber: 2,
        changed: true,
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
        regions: [partition, freeRegion]
    })
    readonly property var systemDevice: ({
        candidateId: "system-device",
        path: "/dev/system",
        model: "System disk",
        sizeBytes: 536870912,
        logicalSectorSize: 512,
        systemDevice: true,
        mounted: true,
        containsData: true,
        blockers: [],
        regions: []
    })
    readonly property var mountedDataDevice: ({
        candidateId: "mounted-data-device",
        path: "/dev/data",
        model: "Mounted data disk",
        sizeBytes: 536870912,
        logicalSectorSize: 512,
        systemDevice: false,
        mounted: true,
        containsData: true,
        blockers: [],
        regions: []
    })

    QtObject { id: editor }

    QtObject {
        id: provisioning
        property var devices: [root.systemDevice, root.mountedDataDevice, root.device]
        property var topology: ({generation: "topology-1", devices: devices})
        property var inspection: ({})
        property var plan: ({
            planId: "plan-free",
            mode: "create-partition-in-unallocated-space",
            deviceId: "device-1",
            freeRegionId: "free-1",
            before: {logicalSectorSize: 512, regions: [root.partition, root.freeRegion]},
            after: {logicalSectorSize: 512, regions: [root.partition, root.plannedPartition]}
        })
        property var operation: ({})
        property var sourceCandidates: ["/home"]
        property bool busy: false
        property string errorMessage: ""
        property int refreshCalls: 0
        property int clearSelectionCalls: 0
        signal completed(string profileId)
        function refresh() { ++refreshCalls }
        function buildPlan(selection, mode) {}
        function inspectExistingTarget(selection, passphrase) {}
        function clearSelection() { ++clearSelectionCalls }
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
        workflowMode: "prepare"
        step: 1
        selectedDevice: root.device
        selectedTarget: root.freeRegion
    }

    Timer {
        interval: 100
        running: true
        repeat: false
        onTriggered: {
            if (!page.freeSpace || !page.hasPlan || !page.planMatchesSelection
                    || page.candidateDevices.length !== 2
                    || page.candidateDevices[0].candidateId !== "mounted-data-device"
                    || page.candidateDevices[1].candidateId !== "device-1"
                    || page.confirmationToken !== "CREATE"
                    || page.partitionManagerUrl.toString() !== "applications:org.kde.partitionmanager.desktop") {
                console.error("Free-space preparation page bindings are invalid")
                Qt.exit(1)
                return
            }
            provisioning.plan = {
                planId: "plan-partition",
                mode: "reformat-existing-partition",
                deviceId: "device-1",
                partitionId: "partition-1",
                before: {logicalSectorSize: 512, regions: [root.partition, root.freeRegion]},
                after: {logicalSectorSize: 512, regions: [root.partition, root.freeRegion]}
            }
            page.selectedTarget = root.partition
            Qt.callLater(function() {
                if (page.freeSpace || !page.planMatchesSelection
                        || page.confirmationToken !== "ERASE-TEST1") {
                    console.error("Partition-specific erase confirmation is invalid")
                    Qt.exit(1)
                    return
                }
                provisioning.plan = {
                    planId: "plan-free-blocked",
                    mode: "create-partition-in-unallocated-space",
                    deviceId: "device-1",
                    freeRegionId: "free-1",
                    before: {logicalSectorSize: 512, regions: [root.partition, root.freeRegion]},
                    after: {logicalSectorSize: 512, regions: [root.partition, root.plannedPartition]}
                }
                page.selectedDevice = {
                    candidateId: "device-1",
                    path: "/dev/test",
                    mounted: true,
                    blockers: ["mounted-filesystem"],
                    regions: [root.partition, root.freeRegion]
                }
                page.selectedTarget = root.freeRegion
                Qt.callLater(function() {
                    if (page.selectedTargetSafe) {
                        console.error("Mounted sibling did not block free-space preparation")
                        Qt.exit(1)
                        return
                    }
                    page.rescanStorage()
                    if (page.selectedDevice !== null || page.selectedTarget !== null
                            || provisioning.clearSelectionCalls !== 1
                            || provisioning.refreshCalls !== 1) {
                        console.error("Storage rescan preserved a stale selection or plan")
                        Qt.exit(1)
                        return
                    }
                    Qt.exit(0)
                })
            })
        }
    }
}
