// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import "../ui" as UI

Item {
    id: root
    width: 800
    height: 720

    function findObject(item, objectName) {
        if (item.objectName === objectName)
            return item
        for (const child of item.children ?? []) {
            const match = root.findObject(child, objectName)
            if (match !== null)
                return match
        }
        return null
    }

    readonly property var partition: ({
        kind: "existing-partition",
        candidateId: "partition-1",
        partitionNumber: 1,
        encrypted: true,
        sectorCount: 1048576,
        mounted: false,
        blockers: [],
        suitableForAdoption: true,
        suitableForReformat: true,
        changed: false,
        dataWillBeErased: false
    })
    readonly property var device: ({
        candidateId: "device-1",
        displayIndex: 5,
        transport: "usb",
        removable: false,
        hotplug: false,
        sizeBytes: 536870912,
        logicalSectorSize: 512,
        partitionTableType: "gpt",
        mounted: false,
        containsData: true,
        blockers: [],
        regions: [partition]
    })
    readonly property var internalDevice: ({
        candidateId: "internal-device",
        displayIndex: 1,
        transport: "nvme",
        removable: false,
        hotplug: false,
        systemDevice: false,
        partitionTableType: "gpt",
        regions: [partition]
    })
    readonly property var systemDevice: ({
        candidateId: "system-device",
        displayIndex: 2,
        transport: "usb",
        removable: true,
        hotplug: true,
        systemDevice: true,
        partitionTableType: "gpt",
        regions: [partition]
    })
    readonly property var mbrDevice: ({
        candidateId: "mbr-device",
        displayIndex: 3,
        transport: "usb",
        removable: true,
        hotplug: true,
        systemDevice: false,
        partitionTableType: "mbr",
        regions: [partition]
    })
    readonly property var unencryptedDevice: ({
        candidateId: "unencrypted-device",
        displayIndex: 4,
        transport: "usb",
        removable: true,
        hotplug: true,
        systemDevice: false,
        partitionTableType: "gpt",
        regions: [{
            kind: "existing-partition",
            candidateId: "plain-partition",
            partitionNumber: 1,
            encrypted: false,
            mounted: false,
            blockers: [],
            suitableForAdoption: false
        }]
    })

    QtObject {
        id: editor
    }

    QtObject {
        id: provisioning
        property var devices: [
            root.internalDevice,
            root.systemDevice,
            root.mbrDevice,
            root.unencryptedDevice,
            root.device
        ]
        property var topology: ({generation: "topology-1", devices: devices})
        property var inspection: ({
            inspectionId: "inspection-1",
            classification: "compatible-repository",
            repositoryId: "repository-1",
            snapshotCount: 3
        })
        property var plan: ({
            planId: "plan-1",
            mode: "adopt-existing-target",
            partitionId: "partition-1",
            before: {logicalSectorSize: 512, regions: [root.partition]},
            after: {logicalSectorSize: 512, regions: [root.partition]}
        })
        property var operation: ({})
        property var sourceCandidates: [{
            id: "source-home",
            path: "/home",
            displayName: "Home folder — /home",
            filesystemUuid: "source-fs",
            mountRoot: "/home",
            localSnapshotRoot: "/home/.snapshots/btrfs-backup"
        }]
        property bool busy: false
        property string errorMessage: ""
        signal completed(string profileId)
        function refresh() {}
        function buildPlan(selection, mode) {}
        function inspectExistingTarget(selection, passphrase) {}
        function clearSelection() {}
        function start(profileId, profileName, sources, passphrase, confirmation, automaticKey) {}
        function poll() {}
        function cancel() {}
        function clearError() {}
        function formatBytes(bytes) { return String(bytes) + " B" }
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
            if (!page.adoption || !page.hasPlan || page.selectedTarget.partitionNumber !== 1
                    || page.candidateDevices.length !== 1
                    || page.candidateDevices[0].candidateId !== "device-1"
                    || page.selectedSources.length !== 1
                    || page.selectedSources[0].candidateId !== "source-home") {
                console.error("Existing target adoption page bindings are invalid")
                Qt.exit(1)
                return
            }
            page.step = 0
            verifyWelcome.start()
        }
    }

    Timer {
        id: verifyWelcome
        interval: 100
        repeat: false
        onTriggered: {
            const welcomeContent = root.findObject(page, "newProfileWelcomeContent")
            const welcomePosition = welcomeContent?.mapToItem(page, 0, 0) ?? null
            if (welcomePosition === null || welcomeContent.width <= 0
                    || Math.abs(welcomePosition.x + welcomeContent.width / 2 - page.width / 2) > 0.5) {
                console.error("New-profile welcome content is not centered horizontally",
                              welcomePosition?.x, welcomeContent?.width, welcomeContent?.parent?.width,
                              page.width)
                Qt.exit(2)
                return
            }
            Qt.exit(0)
        }
    }
}
