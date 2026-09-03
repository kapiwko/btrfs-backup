// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
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
        : "ERASE-" + root.deviceNodeName(root.selectedTarget?.path || root.selectedDevice?.path || "DEVICE")
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
        && (root.selectedTarget.mountPoints?.length ?? 0) === 0
        && (!root.freeSpace || (root.selectedDevice !== null
            && (root.selectedDevice.blockers?.length ?? 0) === 0
            && !root.selectedDevice.mounted))
    readonly property var candidateDevices: (root.provisioning.devices ?? []).filter(device =>
        !(device.systemDevice ?? false)
            && (!root.adoption || (root.deviceIsExternal(device) && root.deviceHasAdoptionCandidate(device))))
    readonly property url partitionManagerUrl: "applications:org.kde.partitionmanager.desktop"
    readonly property string inspectionClassification: root.provisioning.inspection.classification ?? ""

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
            && region.filesystemType === "crypto_LUKS"
            && (region.suitableForAdoption ?? false)
            && (region.blockers?.length ?? 0) === 0)
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
                profileId.text, profileName.text, sourcePath.currentText,
                passphrase.text, root.adoption ? passphrase.text : confirmation.text,
                root.adoption ? false : automaticKey.checked
            );
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: root.step

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing
            Layout.margins: Kirigami.Units.largeSpacing

            Item { Layout.fillHeight: true }
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
                Layout.maximumWidth: Kirigami.Units.gridUnit * 34
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: translations.i18n("Choose how the backup device should be configured.")
                opacity: 0.75
            }
            QQC2.ItemDelegate {
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 34
                Layout.alignment: Qt.AlignHCenter
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
                Layout.maximumWidth: Kirigami.Units.gridUnit * 34
                Layout.alignment: Qt.AlignHCenter
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
            Item { Layout.fillHeight: true }
        }

        QQC2.ScrollView {
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

                RowLayout {
                    Layout.fillWidth: true
                    Kirigami.Icon {
                        source: root.adoption ? "document-import" : "tools-wizard"
                        implicitWidth: Kirigami.Units.iconSizes.medium
                        implicitHeight: implicitWidth
                    }
                    Kirigami.TitleSubtitle {
                        Layout.fillWidth: true
                        title: root.adoption
                            ? translations.i18n("Use existing backup device")
                            : translations.i18n("Prepare backup device")
                        subtitle: root.adoption
                            ? translations.i18n("The existing repository will be verified before it is assigned.")
                            : translations.i18n("Review the storage plan before any data is changed.")
                        selected: false
                    }
                    QQC2.Button {
                        icon.name: "go-previous-symbolic"
                        text: translations.i18n("Change setup type")
                        enabled: !root.provisioning.busy
                        onClicked: root.resetWorkflow()
                    }
                }

                Kirigami.Heading { text: translations.i18n("Select a disk or partition"); level: 2 }
                Kirigami.PlaceholderMessage {
                    Layout.fillWidth: true
                    visible: root.candidateDevices.length === 0 && !root.provisioning.busy
                    icon.name: "drive-removable-media"
                    text: translations.i18n("No suitable backup devices found")
                    helpfulAction: Kirigami.Action {
                        icon.name: "view-refresh-symbolic"
                        text: translations.i18n("Rescan")
                        onTriggered: root.rescanStorage()
                    }
                }
                Repeater {
                    model: root.candidateDevices
                    QQC2.ItemDelegate {
                        id: deviceRow
                        required property var modelData
                        Layout.fillWidth: true
                        enabled: !root.provisioning.busy
                        highlighted: root.selectedDevice?.candidateId === modelData.candidateId
                        onClicked: {
                            root.selectedDevice = modelData
                            if (root.adoption || root.deviceHasConfiguredTarget(modelData)) {
                                root.selectedTarget = null
                                root.provisioning.clearSelection()
                            } else {
                                root.selectedTarget = modelData
                                root.provisioning.buildPlan(modelData, "erase-whole-device")
                            }
                        }
                        contentItem: RowLayout {
                            spacing: Kirigami.Units.largeSpacing
                            Kirigami.Icon {
                                source: "drive-removable-media"
                                implicitWidth: Kirigami.Units.iconSizes.medium
                                implicitHeight: implicitWidth
                            }
                            Kirigami.TitleSubtitle {
                                Layout.fillWidth: true
                                title: (deviceRow.modelData.model || deviceRow.modelData.path)
                                    + " — " + root.formatBytes(deviceRow.modelData.sizeBytes)
                                subtitle: deviceRow.modelData.path + " · "
                                    + (root.deviceHasConfiguredTarget(deviceRow.modelData)
                                        ? translations.i18n("contains a configured backup target")
                                        : deviceRow.modelData.mounted
                                        ? translations.i18n("in use")
                                        : deviceRow.modelData.containsData
                                            ? translations.i18n("contains data")
                                            : translations.i18n("empty"))
                                selected: deviceRow.highlighted
                            }
                            Kirigami.Icon {
                                visible: deviceRow.highlighted
                                source: "emblem-success"
                                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                                implicitHeight: implicitWidth
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.selectedDevice !== null
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading { text: translations.i18n("Partitions and free space"); level: 3 }
                    Repeater {
                        model: root.selectedDevice?.regions ?? []
                        QQC2.ItemDelegate {
                            id: partitionRow
                            required property var modelData
                            Layout.fillWidth: true
                            visible: modelData.kind === "existing-partition" || modelData.kind === "unallocated"
                            enabled: visible && (root.adoption
                                ? modelData.kind === "existing-partition" && modelData.suitableForAdoption
                                : modelData.kind === "unallocated"
                                    ? modelData.suitableForBackupPartition
                                        && (root.selectedDevice.blockers?.length ?? 0) === 0
                                        && !root.selectedDevice.mounted
                                    : modelData.suitableForReformat)
                                && (modelData.blockers?.length ?? 0) === 0
                                && (modelData.mountPoints?.length ?? 0) === 0
                                && !root.provisioning.busy
                            highlighted: root.selectedTarget?.candidateId === modelData.candidateId
                            onClicked: {
                                root.selectedTarget = modelData
                                if (root.adoption)
                                    root.provisioning.clearSelection()
                                else
                                    root.provisioning.buildPlan(
                                        modelData,
                                        modelData.kind === "unallocated"
                                            ? "create-partition-in-unallocated-space"
                                            : "reformat-existing-partition"
                                    )
                            }
                            contentItem: RowLayout {
                                spacing: Kirigami.Units.largeSpacing
                                Kirigami.Icon {
                                    source: partitionRow.modelData.kind === "unallocated"
                                        ? "list-add" : "drive-harddisk"
                                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                                    implicitHeight: implicitWidth
                                }
                                Kirigami.TitleSubtitle {
                                    Layout.fillWidth: true
                                    title: (partitionRow.modelData.kind === "unallocated"
                                        ? translations.i18n("Free space")
                                        : partitionRow.modelData.partitionLabel
                                            || partitionRow.modelData.filesystemLabel
                                            || partitionRow.modelData.path) + " — "
                                        + root.formatBytes(Number(partitionRow.modelData.sectorCount)
                                            * Number(root.selectedDevice.logicalSectorSize || 512))
                                    subtitle: partitionRow.modelData.kind === "unallocated"
                                        ? translations.i18n("Available for a new backup partition")
                                        : partitionRow.modelData.configuredBackupTarget
                                            ? translations.i18n("Already used by a backup profile")
                                            : partitionRow.modelData.path + " · "
                                                + (partitionRow.modelData.filesystemType || translations.i18n("unknown filesystem"))
                                    selected: partitionRow.highlighted
                                }
                                Kirigami.Icon {
                                    visible: partitionRow.highlighted
                                    source: "emblem-success"
                                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                                    implicitHeight: implicitWidth
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        QQC2.Button {
                            icon.name: "partitionmanager"
                            text: translations.i18n("Open KDE Partition Manager")
                            enabled: !root.provisioning.busy
                            onClicked: Qt.openUrlExternally(root.partitionManagerUrl)
                        }
                        QQC2.Button {
                            icon.name: "view-refresh-symbolic"
                            text: translations.i18n("Rescan")
                            enabled: !root.provisioning.busy
                            onClicked: root.rescanStorage()
                        }
                        Item { Layout.fillWidth: true }
                    }
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
                        editable: false
                        displayText: currentIndex >= 0 ? currentText : translations.i18n("Select source Btrfs subvolume")
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
                            root.selectedTarget?.path ?? "",
                            root.confirmationToken
                        )
                        : root.freeSpace
                        ? translations.i18n(
                            "A new partition will be created in the selected free space. Existing partitions will remain unchanged. Type CREATE to confirm."
                        )
                        : translations.i18n(
                            "All data on %1 will be permanently erased. Type %2 to confirm.",
                            root.selectedDevice?.path ?? "",
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

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing
            Item { Layout.fillHeight: true }
            Kirigami.Icon {
                Layout.alignment: Qt.AlignHCenter
                source: root.provisioning.operation.state === "failed"
                    ? "dialog-error" : root.provisioning.operation.phase === "complete"
                        ? "emblem-success" : "drive-removable-media"
                implicitWidth: Kirigami.Units.iconSizes.huge
                implicitHeight: implicitWidth
            }
            QQC2.BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: root.provisioning.operation.state === "queued" || root.provisioning.operation.state === "running"
            }
            Kirigami.Heading {
                Layout.alignment: Qt.AlignHCenter
                text: root.phaseText(root.provisioning.operation.phase || "inspect")
                level: 2
            }
            QQC2.Label {
                Layout.alignment: Qt.AlignHCenter
                text: root.provisioning.operation.state === "failed"
                    ? (root.adoption
                        ? translations.i18n("The existing target could not be assigned. Its data was not modified.")
                        : translations.i18n("Device preparation failed. The disk may require manual recovery."))
                    : (root.adoption
                        ? translations.i18n("Verifying and adding the existing backup target.")
                        : translations.i18n("Do not disconnect the device while preparation is in progress."))
            }
            QQC2.Button {
                Layout.alignment: Qt.AlignHCenter
                visible: root.provisioning.operation.canCancel ?? false
                text: translations.i18n("Cancel")
                onClicked: root.provisioning.cancel()
            }
            Item { Layout.fillHeight: true }
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
    function deviceNodeName(path) {
        const separator = path.lastIndexOf("/")
        return path.substring(separator + 1).toUpperCase()
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
    function formatBytes(value) {
        const gib = Number(value) / 1073741824
        return translations.i18nc("disk size in gibibytes", "%1 GiB", Math.round(gib * 10) / 10)
    }
    function phaseText(phase) {
        switch (phase) {
        case "inspect": return translations.i18n("Inspecting device")
        case "verify-existing-target": return translations.i18n("Verifying existing backup target")
        case "wipe-signatures": return translations.i18n("Erasing existing signatures")
        case "partition": return translations.i18n("Creating partition table")
        case "luks-format": return translations.i18n("Creating LUKS2 encryption")
        case "open": return translations.i18n("Opening encrypted device")
        case "mkfs-btrfs": return translations.i18n("Creating Btrfs filesystem")
        case "close": return translations.i18n("Closing encrypted device")
        case "write-profile": return translations.i18n("Installing backup profile")
        case "complete": return translations.i18n("Backup device is ready")
        default: return phase
        }
    }
}
