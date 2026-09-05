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
    readonly property var selectedSourceCandidate: sourcePath.currentIndex >= 0
        ? root.provisioning.sourceCandidates[sourcePath.currentIndex] : null
    readonly property string localSnapshotDirectory: {
        const snapshotRoot = root.selectedSourceCandidate?.localSnapshotRoot ?? ""
        const id = profileId.text.trim()
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
            && passphrase.text.length > 0
            && (root.adoption && !root.hasPlan
                || profileId.acceptableInput
                    && profileName.text.length > 0 && sourcePath.currentIndex >= 0
                    && (root.adoption || passphrase.text === confirmation.text)
                    && (root.adoption || eraseConfirmation.text === root.confirmationToken)
                    && root.hasPlan
                    && (root.provisioning.plan.mode === "erase-whole-device"
                        || root.provisioning.plan.mode === "reformat-existing-partition"
                        || root.provisioning.plan.mode === "create-partition-in-unallocated-space"
                        || root.provisioning.plan.mode === "adopt-existing-target")
                    && root.planMatchesSelection)
            && !root.provisioning.busy
        onTriggered: {
            if (root.adoption && !root.hasPlan) {
                root.provisioning.inspectExistingTarget(root.selectedTarget, passphrase.text)
                return
            }
            root.step = 2;
            root.provisioning.start(
                profileId.text, profileName.text, sourcePath.currentValue,
                passphrase.text, root.adoption ? passphrase.text : confirmation.text,
                root.adoption ? false : automaticKey.checked
            );
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: root.step

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                objectName: "newProfileWelcomeContent"
                anchors.centerIn: parent
                width: Math.min(
                    Math.max(0, parent.width - Kirigami.Units.largeSpacing * 2),
                    Kirigami.Units.gridUnit * 34
                )
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    Layout.alignment: Qt.AlignHCenter
                    source: "drive-removable-media"
                    implicitWidth: Kirigami.Units.iconSizes.huge
                    implicitHeight: implicitWidth
                }
                Kirigami.Heading {
                    Layout.alignment: Qt.AlignHCenter
                    text: translations.i18n("Add backup profile")
                    level: 1
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: translations.i18n("Choose how the backup device should be configured.")
                    opacity: 0.75
                }
                QQC2.ItemDelegate {
                    Layout.fillWidth: true
                    contentItem: RowLayout {
                        spacing: Kirigami.Units.largeSpacing
                        Kirigami.Icon {
                            source: "document-import"
                            implicitWidth: Kirigami.Units.iconSizes.large
                            implicitHeight: implicitWidth
                        }
                        Kirigami.TitleSubtitle {
                            Layout.fillWidth: true
                            title: translations.i18n("Use a prepared backup device")
                            subtitle: translations.i18n("Assign an existing LUKS2 and Btrfs repository. Nothing will be erased.")
                            selected: false
                        }
                        Kirigami.Icon {
                            source: "go-next-symbolic"
                            implicitWidth: Kirigami.Units.iconSizes.smallMedium
                            implicitHeight: implicitWidth
                        }
                    }
                    onClicked: {
                        root.workflowMode = "adopt"
                        root.step = 1
                        root.provisioning.refresh()
                    }
                }
                QQC2.ItemDelegate {
                    Layout.fillWidth: true
                    contentItem: RowLayout {
                        spacing: Kirigami.Units.largeSpacing
                        Kirigami.Icon {
                            source: "tools-wizard"
                            implicitWidth: Kirigami.Units.iconSizes.large
                            implicitHeight: implicitWidth
                        }
                        Kirigami.TitleSubtitle {
                            Layout.fillWidth: true
                            title: translations.i18n("Prepare a new backup device")
                            subtitle: translations.i18n("Create an encrypted target. The selected disk or partition will be modified.")
                            selected: false
                        }
                        Kirigami.Icon {
                            source: "go-next-symbolic"
                            implicitWidth: Kirigami.Units.iconSizes.smallMedium
                            implicitHeight: implicitWidth
                        }
                    }
                    onClicked: {
                        root.workflowMode = "prepare"
                        root.step = 1;
                        root.provisioning.refresh();
                    }
                }
            }
        }

        QQC2.ScrollView {
            objectName: "newProfileStoragePage"
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: Kirigami.Units.largeSpacing

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    type: Kirigami.MessageType.Error
                    visible: root.provisioning.errorMessage.length > 0
                    text: root.provisioning.errorMessage
                    showCloseButton: true
                    onVisibleChanged: if (!visible) root.provisioning.clearError()
                }

                StorageTargetSelection {
                    Layout.fillWidth: true
                    workflow: root
                    translations: translations
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.provisioning.plan.planId !== undefined
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading { text: translations.i18n("Before"); level: 3 }
                    StorageLayout {
                        Layout.fillWidth: true
                        regions: root.provisioning.plan.before?.regions ?? []
                        selectedRegionId: root.provisioning.plan.partitionId
                            ?? root.provisioning.plan.freeRegionId ?? ""
                        preview: false
                        logicalSectorSize: root.provisioning.plan.before?.logicalSectorSize ?? 512
                        formatBytes: value => root.provisioning.formatBytes(value)
                    }
                    Kirigami.Heading {
                        text: root.adoption ? translations.i18n("After adoption") : translations.i18n("After preparation")
                        level: 3
                    }
                    StorageLayout {
                        Layout.fillWidth: true
                        regions: root.provisioning.plan.after?.regions ?? []
                        selectedRegionId: root.provisioning.plan.partitionId ?? "planned-backup-partition"
                        preview: true
                        logicalSectorSize: root.provisioning.plan.after?.logicalSectorSize ?? 512
                        formatBytes: value => root.provisioning.formatBytes(value)
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: root.provisioning.plan.mode === "adopt-existing-target"
                            ? translations.i18n("The existing repository and all data on this partition will remain unchanged.")
                            : root.provisioning.plan.mode === "reformat-existing-partition"
                            ? translations.i18n("Only data on the selected partition will be removed. Other partitions will remain unchanged.")
                            : root.provisioning.plan.mode === "create-partition-in-unallocated-space"
                            ? translations.i18n("A new LUKS2 and Btrfs backup partition will be created in the selected free space. Existing partitions will remain unchanged.")
                            : translations.i18n("All existing partitions on this disk will be removed. The resulting backup target will use LUKS2 encryption and Btrfs.")
                    }
                }

                Kirigami.Separator { Layout.fillWidth: true }
                Kirigami.Heading {
                    text: translations.i18n("Profile details")
                    level: 2
                }
                Kirigami.FormLayout {
                    Layout.fillWidth: true
                    QQC2.TextField {
                        id: profileName
                        Layout.fillWidth: true
                        Kirigami.FormData.label: translations.i18n("Profile name:")
                        placeholderText: translations.i18n("Profile name")
                        onTextEdited: if (!profileId.modified) profileId.text = root.slug(text)
                    }
                    QQC2.TextField {
                        id: profileId
                        property bool modified: false
                        Layout.fillWidth: true
                        Kirigami.FormData.label: translations.i18n("Profile identifier:")
                        placeholderText: translations.i18n("Profile identifier")
                        validator: RegularExpressionValidator { regularExpression: /^[a-z0-9][a-z0-9-]{0,62}$/ }
                        onTextEdited: modified = true
                    }
                    QQC2.ComboBox {
                        id: sourcePath
                        Layout.fillWidth: true
                        Kirigami.FormData.label: translations.i18n("Backup source:")
                        model: root.provisioning.sourceCandidates
                        textRole: "displayName"
                        valueRole: "id"
                        editable: false
                        displayText: currentIndex >= 0 ? currentText : translations.i18n("Select source Btrfs subvolume")
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        Kirigami.FormData.label: translations.i18n("Local snapshots:")
                        text: root.localSnapshotDirectory
                        visible: text.length > 0
                        elide: Text.ElideMiddle
                    }
                }

                Kirigami.Heading {
                    text: translations.i18n("Encryption")
                    level: 2
                }
                Kirigami.FormLayout {
                    Layout.fillWidth: true
                    QQC2.CheckBox {
                        id: automaticKey
                        Layout.fillWidth: true
                        Kirigami.FormData.label: translations.i18n("Automatic backups:")
                        checked: true
                        visible: !root.adoption
                        text: translations.i18n("Create a protected key for automatic backups")
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: automaticKey.visible && automaticKey.checked
                        wrapMode: Text.Wrap
                        text: translations.i18n("The key is stored in a root-only system directory and allows backups to unlock the target without a prompt. The recovery passphrase remains available if the key is lost or removed.")
                        opacity: 0.75
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Kirigami.FormData.label: root.adoption
                            ? translations.i18n("Existing passphrase:")
                            : translations.i18n("Recovery passphrase:")
                        QQC2.TextField {
                            id: passphrase
                            Layout.fillWidth: true
                            placeholderText: root.adoption
                                ? translations.i18n("Existing target passphrase")
                                : translations.i18n("Recovery passphrase")
                            echoMode: root.passwordVisible ? TextInput.Normal : TextInput.Password
                        }
                        QQC2.ToolButton {
                            icon.name: root.passwordVisible ? "view-hidden-symbolic" : "view-visible-symbolic"
                            text: root.passwordVisible
                                ? translations.i18n("Hide password")
                                : translations.i18n("Show password")
                            display: QQC2.AbstractButton.IconOnly
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.text: text
                            onClicked: root.passwordVisible = !root.passwordVisible
                        }
                    }
                    QQC2.TextField {
                        id: confirmation
                        Layout.fillWidth: true
                        Kirigami.FormData.label: translations.i18n("Confirm passphrase:")
                        visible: !root.adoption
                        placeholderText: translations.i18n("Confirm recovery passphrase")
                        echoMode: root.passwordVisible ? TextInput.Normal : TextInput.Password
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: !root.adoption && confirmation.text.length > 0
                        && passphrase.text !== confirmation.text
                    type: Kirigami.MessageType.Error
                    text: translations.i18n("The passphrases do not match.")
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: root.selectedTarget !== null
                    type: root.adoption && (root.inspectionClassification === "" || root.hasPlan)
                        ? Kirigami.MessageType.Information : Kirigami.MessageType.Warning
                    text: root.adoption
                        ? (root.inspectionClassification === "empty-filesystem"
                            ? translations.i18n("This encrypted Btrfs filesystem is empty and does not contain a backup repository.")
                            : root.inspectionClassification === "legacy-repository"
                            ? translations.i18n("This target uses an older backup layout and cannot be adopted automatically.")
                            : root.inspectionClassification === "unsupported-repository"
                            ? translations.i18n("This repository format is not supported by this version of btrfs-backup.")
                            : root.inspectionClassification === "foreign-or-invalid-repository"
                            ? translations.i18n("This target does not contain a valid btrfs-backup repository.")
                            : root.inspectionClassification === "not-btrfs-filesystem"
                            ? translations.i18n("The encrypted target does not contain a Btrfs filesystem.")
                            : root.provisioning.inspection.repositoryId
                            ? translations.i18np(
                                "Repository %2 contains %1 snapshot. No data will be modified.",
                                "Repository %2 contains %1 snapshots. No data will be modified.",
                                Number(root.provisioning.inspection.snapshotCount),
                                root.provisioning.inspection.repositoryId
                            )
                            : translations.i18n("The target will be opened read-only and verified before it can be assigned."))
                        : root.provisioning.plan.mode === "reformat-existing-partition"
                        ? translations.i18n(
                            "All data on partition %1 will be permanently erased. Other partitions will remain unchanged. Type %2 to confirm.",
                            root.selectedTarget?.partitionNumber ?? "",
                            root.confirmationToken
                        )
                        : root.freeSpace
                        ? translations.i18n(
                            "A new partition will be created in the selected free space. Existing partitions will remain unchanged. Type CREATE to confirm."
                        )
                        : translations.i18n(
                            "All data on %1 will be permanently erased. Type %2 to confirm.",
                            translations.i18n("the selected storage device"),
                            root.confirmationToken
                        )
                }
                QQC2.TextField {
                    id: eraseConfirmation
                    Layout.fillWidth: true
                    visible: root.selectedDevice !== null && !root.adoption
                    Accessible.name: translations.i18n("Destructive operation confirmation")
                    placeholderText: root.confirmationToken
                }
            }
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
