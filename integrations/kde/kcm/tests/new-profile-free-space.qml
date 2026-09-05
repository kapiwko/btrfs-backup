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

    function fitsInside(item, container) {
        const position = item.mapToItem(container, 0, 0)
        return position.x >= -1 && position.x + item.width <= container.width + 1
    }

    readonly property var partition: ({
        kind: "existing-partition",
        candidateId: "partition-1",
        partitionNumber: 1,
        encrypted: false,
        sectorCount: 524288,
        mounted: false,
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
        displayIndex: 3,
        sizeBytes: 536870912,
        logicalSectorSize: 512,
        mounted: false,
        containsData: true,
        blockers: [],
        regions: [partition, freeRegion]
    })
    readonly property var systemDevice: ({
        candidateId: "system-device",
        displayIndex: 1,
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
        displayIndex: 2,
        sizeBytes: 536870912,
        logicalSectorSize: 512,
        systemDevice: false,
        mounted: true,
        containsData: true,
        blockers: [],
        regions: []
    })
    readonly property var blockedDevice: ({
        candidateId: "blocked-device",
        displayIndex: 4,
        sizeBytes: 536870912,
        logicalSectorSize: 512,
        systemDevice: false,
        mounted: false,
        containsData: true,
        blockers: [{code: "unsupported-block-stack"}],
        regions: []
    })

    QtObject { id: editor }

    QtObject {
        id: provisioning
        property var devices: [root.systemDevice, root.mountedDataDevice, root.blockedDevice, root.device]
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
        property var sourceCandidates: [{
            id: "source-home",
            path: "/home",
            filesystemUuid: "source-fs",
            mountRoot: "/home",
            localSnapshotRoot: "/home/.snapshots/btrfs-backup"
        }]
        property bool busy: false
        property string errorMessage: ""
        property int refreshCalls: 0
        property int clearSelectionCalls: 0
        property int buildPlanCalls: 0
        property string lastBuildMode: ""
        signal completed(string profileId)
        function refresh() { ++refreshCalls }
        function buildPlan(selection, mode) { ++buildPlanCalls; lastBuildMode = mode }
        function inspectExistingTarget(selection, passphrase) {}
        function clearSelection() { ++clearSelectionCalls }
        function start(profileId, profileName, source, passphrase, confirmation, automaticKey) {}
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
                    || page.candidateDevices.length !== 1
                    || page.candidateDevices[0].candidateId !== "device-1"
                    || page.unavailableDevices.length !== 3
                    || page.selectedSourceCandidate?.id !== "source-home") {
                console.error("Free-space source candidate bindings are invalid")
                Qt.exit(1)
                return
            }
            if (page.unavailableReason(root.blockedDevice)
                    === page.blockerReason({code: "unknown"})) {
                console.error("Blocked storage device reason is not presented")
                Qt.exit(1)
                return
            }
            const unavailableToggle = root.findObject(page, "unavailableDevicesToggle")
            const unavailableView = root.findObject(page, "unavailableDevicesView")
            if (unavailableToggle === null || unavailableView === null || unavailableView.visible) {
                console.error("Unavailable storage devices are not collapsed initially")
                Qt.exit(1)
                return
            }
            if (unavailableToggle.width >= page.width / 2) {
                console.error("Unavailable storage device toggle is too prominent")
                Qt.exit(1)
                return
            }
            unavailableToggle.checked = true
            if (!unavailableView.visible) {
                console.error("Unavailable storage devices cannot be expanded")
                Qt.exit(1)
                return
            }
            unavailableToggle.checked = false
            if (unavailableView.visible) {
                console.error("Unavailable storage devices cannot be collapsed again")
                Qt.exit(1)
                return
            }
            const profileDetailsForm = root.findObject(page, "profileDetailsForm")
            const encryptionForm = root.findObject(page, "encryptionForm")
            const profileIdentifierHelp = root.findObject(page, "profileIdentifierHelp")
            const automaticKeyField = root.findObject(page, "automaticKeyField")
            const automaticKeyExplanation = root.findObject(page, "automaticKeyExplanation")
            const passphraseField = root.findObject(page, "passphraseField")
            const confirmationField = root.findObject(page, "confirmationField")
            if (profileDetailsForm === null || encryptionForm === null
                    || profileIdentifierHelp === null || profileIdentifierHelp.text.length === 0
                    || automaticKeyField === null || automaticKeyExplanation === null
                    || passphraseField === null || confirmationField === null
                    || profileDetailsForm.width > page.width
                    || encryptionForm.width > page.width
                    || !root.fitsInside(automaticKeyField, encryptionForm)
                    || !root.fitsInside(automaticKeyExplanation, encryptionForm)
                    || !root.fitsInside(passphraseField, encryptionForm)
                    || !root.fitsInside(confirmationField, encryptionForm)) {
                console.error("Provisioning forms are not bounded or documented")
                Qt.exit(1)
                return
            }
            if (page.confirmationToken !== "CREATE") {
                console.error("Free-space preparation page bindings are invalid")
                Qt.exit(1)
                return
            }
            provisioning.operation = {
                operationId: "prepare-test",
                state: "failed",
                phase: "write-profile",
                lastCompletedPhase: "close",
                cleanupResult: "mapper-closed",
                errorCode: "device-preparation.profile-conflict",
                recoveryAction: "Review the prepared target."
            }
            if (!page.phaseCompleted("close") || page.phaseCompleted("write-profile")
                    || !page.diagnosticReport().includes("cleanupResult=mapper-closed")) {
                console.error("Structured provisioning recovery state is invalid")
                Qt.exit(1)
                return
            }
            page.selectedTarget = null
            provisioning.plan = ({})
            const wholeDeviceChoice = root.findObject(page, "wholeDeviceChoice")
            if (wholeDeviceChoice === null || !wholeDeviceChoice.enabled) {
                console.error("Explicit whole-device choice is unavailable")
                Qt.exit(1)
                return
            }
            wholeDeviceChoice.clicked()
            if (page.selectedTarget?.candidateId !== "device-1"
                    || provisioning.buildPlanCalls !== 1
                    || provisioning.lastBuildMode !== "erase-whole-device") {
                console.error("Whole-device plan was not created by an explicit choice")
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
                        || page.confirmationToken !== "ERASE-PARTITION-1") {
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
                    displayIndex: 3,
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
                    page.resetWorkflow()
                    Qt.callLater(function() {
                        const welcome = root.findObject(page, "newProfileWelcomeContent")
                        if (page.step !== 0 || welcome === null || !welcome.visible) {
                            console.error("Returning from device preparation did not restore the setup choices")
                            Qt.exit(1)
                            return
                        }
                        page.workflowMode = "prepare"
                        page.step = 1
                        provisioning.refresh()
                        Qt.callLater(function() {
                            const storagePage = root.findObject(page, "newProfileStoragePage")
                            if (page.step !== 1 || storagePage === null || !storagePage.visible
                                    || storagePage.width <= 0 || storagePage.height <= 0) {
                                console.error("Re-entering device preparation produced an empty page")
                                Qt.exit(1)
                                return
                            }
                            Qt.exit(0)
                        })
                    })
                })
            })
        }
    }
}
