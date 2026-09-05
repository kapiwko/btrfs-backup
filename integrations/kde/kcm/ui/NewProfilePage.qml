// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

KCMUtils.AbstractKCM {
    id: root
    required property var editor
    required property var provisioning
    property int step: 0
    property string workflowMode: ""
    property var selectedDevice: null
    property var selectedTarget: null
    property bool passwordVisible: false
    readonly property bool adoption: root.workflowMode === "adopt"
    readonly property bool hasPlan: (root.provisioning.plan.planId ?? "") !== ""
    readonly property string planMode: root.provisioning.plan.mode ?? ""
    readonly property bool freeSpace: root.planMode === "create-partition-in-unallocated-space"
    readonly property string confirmationToken: root.adoption ? ""
        : root.freeSpace ? "CREATE"
        : root.planMode === "reformat-existing-partition"
            ? "ERASE-PARTITION-" + String(root.selectedTarget?.partitionNumber ?? "")
            : "ERASE-DISK"
    readonly property bool planMatchesSelection: {
        if (!root.hasPlan || root.selectedTarget === null)
            return false
        if (root.planMode === "erase-whole-device")
            return root.provisioning.plan.deviceId === root.selectedTarget.candidateId
        if (root.freeSpace)
            return root.provisioning.plan.freeRegionId === root.selectedTarget.candidateId
        return root.provisioning.plan.partitionId === root.selectedTarget.candidateId
    }
    readonly property bool selectedTargetSafe: root.selectedTarget !== null
        && (root.selectedTarget.blockers?.length ?? 0) === 0
        && !root.selectedTarget.mounted
        && (!root.freeSpace || (root.selectedDevice !== null
            && (root.selectedDevice.blockers?.length ?? 0) === 0
            && !root.selectedDevice.mounted))
    readonly property var candidateDevices: (root.provisioning.devices ?? []).filter(device => root.deviceAvailable(device))
    readonly property var unavailableDevices: (root.provisioning.devices ?? []).filter(device => !root.deviceAvailable(device))
    readonly property string inspectionClassification: root.provisioning.inspection.classification ?? ""
    readonly property var selectedSourceCandidate: setupPage.sourceCurrentIndex >= 0
        ? root.provisioning.sourceCandidates[setupPage.sourceCurrentIndex] : null
    readonly property string localSnapshotDirectory: {
        const snapshotRoot = root.selectedSourceCandidate?.localSnapshotRoot ?? ""
        const id = setupPage.profileIdentifier.trim()
        if (snapshotRoot === "" || id === "")
            return ""
        return snapshotRoot.endsWith("/") ? snapshotRoot + id : snapshotRoot + "/" + id
    }

    function deviceHasConfiguredTarget(device) {
        const regions = device?.regions ?? []
        for (const region of regions) {
            if (region.configuredBackupTarget)
                return true
        }
        return false
    }

    function deviceIsExternal(device) {
        const transport = String(device?.transport ?? "").toLowerCase()
        return (device?.removable ?? false) || (device?.hotplug ?? false)
            || transport === "usb" || transport === "firewire" || transport === "thunderbolt"
    }

    function deviceHasAdoptionCandidate(device) {
        if (String(device?.partitionTableType ?? "").toLowerCase() !== "gpt")
            return false
        const regions = device?.regions ?? []
        return regions.some(region => region.kind === "existing-partition"
            && (region.encrypted ?? false)
            && (region.suitableForAdoption ?? false)
            && (region.blockers?.length ?? 0) === 0)
    }

    function deviceAvailable(device) {
        return !(device?.systemDevice ?? false)
            && !(device?.mounted ?? false)
            && (device?.blockers?.length ?? 0) === 0
            && (!root.adoption || (root.deviceIsExternal(device) && root.deviceHasAdoptionCandidate(device)))
    }

    function blockerReason(blocker) {
        const code = typeof blocker === "string" ? blocker : String(blocker?.code ?? "")
        switch (code) {
        case "active-swap": return translations.i18n("The device is active swap space")
        case "block-holder": return translations.i18n("The device is used by another storage layer")
        case "mounted-filesystem": return translations.i18n("The device contains a mounted filesystem")
        case "read-only-device": return translations.i18n("The device is read-only")
        case "unsupported-block-stack": return translations.i18n("The device is managed by an unsupported storage stack")
        case "ambiguous-signatures": return translations.i18n("The device contains ambiguous filesystem signatures")
        case "holder-state-unavailable":
        case "dependency-state-unavailable":
        case "signature-state-unavailable":
            return translations.i18n("The device safety state could not be verified")
        default: return translations.i18n("The device is not safe to use as a backup target")
        }
    }

    function unavailableReason(device) {
        if (device?.systemDevice ?? false)
            return translations.i18n("System disk — it cannot be used as a backup target")
        if (device?.mounted ?? false)
            return translations.i18n("The device or one of its partitions is mounted")
        if ((device?.blockers?.length ?? 0) > 0)
            return root.blockerReason(device.blockers[0])
        if (root.adoption && !root.deviceIsExternal(device))
            return translations.i18n("Only removable or externally connected targets can be adopted")
        if (root.adoption && !root.deviceHasAdoptionCandidate(device))
            return translations.i18n("No compatible LUKS2 and Btrfs repository was found")
        return translations.i18n("The device is not safe to use as a backup target")
    }


    function regionUnavailableReason(region) {
        if (region?.mounted ?? false)
            return translations.i18n("This partition is mounted")
        if ((region?.blockers?.length ?? 0) > 0)
            return root.blockerReason(region.blockers[0])
        if (root.adoption && !(region?.suitableForAdoption ?? false))
            return translations.i18n("This partition is not a compatible backup target")
        if (region?.kind === "unallocated" && !(region?.suitableForBackupPartition ?? false))
            return translations.i18n("This free area cannot hold a backup partition")
        if (region?.kind === "existing-partition" && !(region?.suitableForReformat ?? false))
            return translations.i18n("This partition cannot be safely reformatted")
        return ""
    }

    function operationSteps() {
        if (root.adoption)
            return ["verify-existing-target", "write-profile"]
        if (root.planMode === "erase-whole-device")
            return ["backup-partition-table", "wipe-signatures", "partition", "luks-format", "open", "mkfs-btrfs", "close", "write-profile", "credentials"]
        if (root.planMode === "create-partition-in-unallocated-space")
            return ["backup-partition-table", "partition", "luks-format", "open", "mkfs-btrfs", "close", "write-profile", "credentials"]
        return ["wipe-signatures", "luks-format", "open", "mkfs-btrfs", "close", "write-profile", "credentials"]
    }

    function phaseCompleted(phase) {
        const steps = root.operationSteps()
        const completed = steps.indexOf(root.provisioning.operation.lastCompletedPhase ?? "")
        return completed >= 0 && steps.indexOf(phase) <= completed
    }

    function diagnosticReport() {
        return "operationId=" + (root.provisioning.operation.operationId ?? "")
            + "\nstate=" + (root.provisioning.operation.state ?? "")
            + "\nphase=" + (root.provisioning.operation.phase ?? "")
            + "\nlastCompletedPhase=" + (root.provisioning.operation.lastCompletedPhase ?? "")
            + "\ncleanupResult=" + (root.provisioning.operation.cleanupResult ?? "")
            + "\nerrorCode=" + (root.provisioning.operation.errorCode ?? "")
    }

    title: root.step === 0 ? translations.i18n("Add backup profile")
        : root.step === 1 ? (root.adoption
            ? translations.i18n("Use existing backup device")
            : translations.i18n("Prepare backup device"))
        : (root.adoption
            ? translations.i18n("Adding backup device")
            : translations.i18n("Preparing backup device"))

    KI18n.KI18nContext { id: translations; translationDomain: "kcm_btrfsbackup" }

    actions: Kirigami.Action {
        icon.name: root.adoption ? "document-import-symbolic" : "tools-wizard-symbolic"
        text: root.adoption
            ? (root.hasPlan ? translations.i18n("Use existing target") : translations.i18n("Inspect target"))
            : root.freeSpace ? translations.i18n("Create and prepare") : translations.i18n("Erase and prepare")
        visible: root.step === 1
        enabled: root.selectedTargetSafe
            && setupPage.passphrase.length > 0
            && (root.adoption && !root.hasPlan
                || setupPage.profileIdentifierAcceptable
                    && setupPage.profileName.length > 0 && setupPage.sourceCurrentIndex >= 0
                    && (root.adoption || setupPage.passphrase === setupPage.confirmation)
                    && (root.adoption || setupPage.eraseConfirmation === root.confirmationToken)
                    && root.hasPlan
                    && (root.provisioning.plan.mode === "erase-whole-device"
                        || root.provisioning.plan.mode === "reformat-existing-partition"
                        || root.provisioning.plan.mode === "create-partition-in-unallocated-space"
                        || root.provisioning.plan.mode === "adopt-existing-target")
                    && root.planMatchesSelection)
            && !root.provisioning.busy
        onTriggered: {
            if (root.adoption && !root.hasPlan) {
                root.provisioning.inspectExistingTarget(root.selectedTarget, setupPage.passphrase)
                return
            }
            root.step = 2;
            root.provisioning.start(
                setupPage.profileIdentifier, setupPage.profileName, setupPage.sourceCurrentValue,
                setupPage.passphrase, root.adoption ? setupPage.passphrase : setupPage.confirmation,
                root.adoption ? false : setupPage.automaticKey
            );
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: root.step

        ProvisioningWelcomePage {
            workflow: root
            translations: translations
        }

        ProvisioningSetupPage {
            id: setupPage
            workflow: root
            translations: translations
        }

        ProvisioningProgressPage {
            workflow: root
            translations: translations
        }
    }

    Timer {
        interval: 750
        repeat: true
        running: root.step === 2 && (root.provisioning.operation.state === "queued" || root.provisioning.operation.state === "running")
        onTriggered: root.provisioning.poll()
    }

    Connections {
        target: root.provisioning
        function onCompleted(profileId) {
            if (typeof kcm !== "undefined")
                kcm.pop();
        }
    }

    function slug(value) {
        return value.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "").substring(0, 63)
    }
    function rescanStorage() {
        root.selectedDevice = null
        root.selectedTarget = null
        root.provisioning.clearSelection()
        root.provisioning.refresh()
    }
    function resetWorkflow() {
        root.workflowMode = ""
        root.step = 0
        root.selectedDevice = null
        root.selectedTarget = null
        root.provisioning.clearSelection()
    }
    function phaseText(phase) {
        switch (phase) {
        case "inspect": return translations.i18n("Inspecting device")
        case "backup-partition-table": return translations.i18n("Saving the current partition table")
        case "verify-existing-target": return translations.i18n("Verifying existing backup target")
        case "wipe-signatures": return translations.i18n("Erasing existing signatures")
        case "partition": return translations.i18n("Creating partition table")
        case "luks-format": return translations.i18n("Creating LUKS2 encryption")
        case "open": return translations.i18n("Opening encrypted device")
        case "mkfs-btrfs": return translations.i18n("Creating Btrfs filesystem")
        case "close": return translations.i18n("Closing encrypted device")
        case "write-profile": return translations.i18n("Installing backup profile")
        case "credentials": return translations.i18n("Installing backup credentials")
        case "complete": return translations.i18n("Backup device is ready")
        default: return phase
        }
    }
}
