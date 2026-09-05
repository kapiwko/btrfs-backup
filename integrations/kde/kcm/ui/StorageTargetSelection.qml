// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    required property var workflow
    required property var translations

    spacing: Kirigami.Units.largeSpacing

    RowLayout {
        Layout.fillWidth: true
        Kirigami.Icon {
            source: root.workflow.adoption ? "document-import" : "tools-wizard"
            implicitWidth: Kirigami.Units.iconSizes.medium
            implicitHeight: implicitWidth
        }
        Kirigami.TitleSubtitle {
            Layout.fillWidth: true
            title: root.workflow.adoption
                ? root.translations.i18n("Use existing backup device")
                : root.translations.i18n("Prepare backup device")
            subtitle: root.workflow.adoption
                ? root.translations.i18n("The existing repository will be verified before it is assigned.")
                : root.translations.i18n("Review the storage plan before any data is changed.")
            selected: false
        }
        QQC2.Button {
            icon.name: "go-previous-symbolic"
            text: root.translations.i18n("Change setup type")
            enabled: !root.workflow.provisioning.busy
            onClicked: root.workflow.resetWorkflow()
        }
    }

    Kirigami.Heading {
        text: root.translations.i18n("Select a disk or partition")
        level: 2
    }
    Kirigami.PlaceholderMessage {
        Layout.fillWidth: true
        visible: root.workflow.candidateDevices.length === 0 && !root.workflow.provisioning.busy
        icon.name: "drive-removable-media"
        text: root.translations.i18n("No suitable backup devices found")
        helpfulAction: Kirigami.Action {
            icon.name: "view-refresh-symbolic"
            text: root.translations.i18n("Rescan")
            onTriggered: root.workflow.rescanStorage()
        }
    }
    Repeater {
        model: root.workflow.candidateDevices
        QQC2.ItemDelegate {
            id: deviceRow
            required property var modelData
            Layout.fillWidth: true
            enabled: !root.workflow.provisioning.busy
            highlighted: root.workflow.selectedDevice?.candidateId === modelData.candidateId
            onClicked: {
                root.workflow.selectedDevice = modelData
                root.workflow.selectedTarget = null
                root.workflow.provisioning.clearSelection()
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
                    title: root.translations.i18n("Storage device %1", deviceRow.modelData.displayIndex)
                        + " — " + root.workflow.provisioning.formatBytes(deviceRow.modelData.sizeBytes)
                    subtitle: (deviceRow.modelData.transport || root.translations.i18n("unknown connection"))
                        + " · " + (root.workflow.deviceHasConfiguredTarget(deviceRow.modelData)
                            ? root.translations.i18n("contains a configured backup target")
                            : deviceRow.modelData.mounted
                            ? root.translations.i18n("in use")
                            : deviceRow.modelData.containsData
                                ? root.translations.i18n("contains data")
                                : root.translations.i18n("empty"))
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
        visible: root.workflow.unavailableDevices.length > 0
        spacing: Kirigami.Units.smallSpacing

        QQC2.Button {
            id: unavailableDevicesToggle

            objectName: "unavailableDevicesToggle"
            Layout.fillWidth: true
            checkable: true
            icon.name: checked ? "arrow-up" : "arrow-down"
            text: root.translations.i18np(
                "%1 unavailable device",
                "%1 unavailable devices",
                root.workflow.unavailableDevices.length
            )
            Accessible.description: checked
                ? root.translations.i18n("Hide unavailable devices")
                : root.translations.i18n("Show unavailable devices")
        }

        QQC2.ScrollView {
            id: unavailableDevicesView

            objectName: "unavailableDevicesView"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(
                unavailableDevicesList.contentHeight,
                Kirigami.Units.gridUnit * 12
            )
            visible: unavailableDevicesToggle.checked
            clip: true

            ListView {
                id: unavailableDevicesList

                objectName: "unavailableDevicesList"
                model: root.workflow.unavailableDevices
                boundsBehavior: Flickable.StopAtBounds
                spacing: Kirigami.Units.smallSpacing

                delegate: QQC2.ItemDelegate {
                    id: unavailableDeviceRow

                    required property var modelData
                    width: ListView.view.width
                    enabled: false
                    contentItem: Kirigami.TitleSubtitle {
                        title: root.translations.i18n(
                            "Storage device %1",
                            unavailableDeviceRow.modelData.displayIndex
                        ) + " — " + root.workflow.provisioning.formatBytes(
                            unavailableDeviceRow.modelData.sizeBytes
                        )
                        subtitle: root.workflow.unavailableReason(unavailableDeviceRow.modelData)
                        selected: false
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: root.workflow.selectedDevice !== null
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            text: root.translations.i18n("How do you want to use this device?")
            level: 3
        }
        QQC2.ItemDelegate {
            id: wholeDeviceRow
            objectName: "wholeDeviceChoice"
            Layout.fillWidth: true
            visible: !root.workflow.adoption
                && !root.workflow.deviceHasConfiguredTarget(root.workflow.selectedDevice)
            enabled: visible
                && (root.workflow.selectedDevice?.blockers?.length ?? 0) === 0
                && !(root.workflow.selectedDevice?.mounted ?? false)
                && !root.workflow.provisioning.busy
            highlighted: root.workflow.selectedTarget?.candidateId
                    === root.workflow.selectedDevice?.candidateId
                && root.workflow.planMode === "erase-whole-device"
            contentItem: Kirigami.TitleSubtitle {
                title: root.translations.i18n("Use the entire device")
                subtitle: root.translations.i18n("Erase every partition and create a new encrypted backup target")
                selected: wholeDeviceRow.highlighted
            }
            onClicked: {
                root.workflow.selectedTarget = root.workflow.selectedDevice
                root.workflow.provisioning.buildPlan(root.workflow.selectedDevice, "erase-whole-device")
            }
        }
        Kirigami.Heading {
            text: root.translations.i18n("Partitions and free space")
            level: 4
        }
        Repeater {
            model: root.workflow.selectedDevice?.regions ?? []
            QQC2.ItemDelegate {
                id: partitionRow
                required property var modelData
                Layout.fillWidth: true
                visible: modelData.kind === "existing-partition" || modelData.kind === "unallocated"
                enabled: visible && (root.workflow.adoption
                    ? modelData.kind === "existing-partition" && modelData.suitableForAdoption
                    : modelData.kind === "unallocated"
                        ? modelData.suitableForBackupPartition
                            && (root.workflow.selectedDevice.blockers?.length ?? 0) === 0
                            && !root.workflow.selectedDevice.mounted
                        : modelData.suitableForReformat)
                    && (modelData.blockers?.length ?? 0) === 0
                    && !modelData.mounted
                    && !root.workflow.provisioning.busy
                highlighted: root.workflow.selectedTarget?.candidateId === modelData.candidateId
                onClicked: {
                    root.workflow.selectedTarget = modelData
                    if (root.workflow.adoption)
                        root.workflow.provisioning.clearSelection()
                    else
                        root.workflow.provisioning.buildPlan(
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
                            ? root.translations.i18n("Free space")
                            : root.translations.i18n(
                                "Partition %1",
                                partitionRow.modelData.partitionNumber
                            )) + " — "
                            + root.workflow.provisioning.formatBytes(
                                Number(partitionRow.modelData.sectorCount)
                                    * Number(root.workflow.selectedDevice.logicalSectorSize || 512)
                            )
                        subtitle: !partitionRow.enabled
                            ? root.workflow.regionUnavailableReason(partitionRow.modelData)
                            : partitionRow.modelData.kind === "unallocated"
                                ? root.translations.i18n("Available for a new backup partition")
                                : partitionRow.modelData.configuredBackupTarget
                                ? root.translations.i18n("Already used by a backup profile")
                                : partitionRow.modelData.encrypted
                                    ? root.translations.i18n("encrypted partition")
                                    : root.translations.i18n("existing partition")
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
                readonly property bool partitionManagerInstalled: typeof kcm !== "undefined"
                    && kcm.partitionManagerAvailable
                icon.name: partitionManagerInstalled ? "partitionmanager" : "system-software-install"
                text: partitionManagerInstalled
                    ? root.translations.i18n("Open KDE Partition Manager")
                    : root.translations.i18n("Install KDE Partition Manager")
                enabled: !root.workflow.provisioning.busy
                onClicked: if (typeof kcm !== "undefined") kcm.openPartitionManager()
            }
            QQC2.Button {
                icon.name: "view-refresh-symbolic"
                text: root.translations.i18n("Rescan")
                enabled: !root.workflow.provisioning.busy
                onClicked: root.workflow.rescanStorage()
            }
            Item { Layout.fillWidth: true }
        }
    }
}
