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
    property var selectedDevice: null

    title: root.step === 0 ? translations.i18n("Add backup profile")
        : root.step === 1 ? translations.i18n("Prepare backup device")
        : translations.i18n("Preparing backup device")

    KI18n.KI18nContext { id: translations; translationDomain: "kcm_btrfsbackup" }

    actions: Kirigami.Action {
        icon.name: "tools-wizard-symbolic"
        text: translations.i18n("Erase and prepare")
        visible: root.step === 1
        enabled: root.selectedDevice !== null && profileId.acceptableInput
            && profileName.text.length > 0 && sourcePath.currentIndex >= 0
            && passphrase.text.length > 0 && passphrase.text === confirmation.text
            && eraseConfirmation.text === "ERASE" && !root.provisioning.busy
        onTriggered: {
            root.step = 2;
            root.provisioning.start(
                profileId.text, profileName.text, root.selectedDevice, sourcePath.currentText,
                passphrase.text, confirmation.text, automaticKey.checked
            );
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: root.step

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing
            QQC2.ItemDelegate {
                Layout.fillWidth: true
                enabled: false
                contentItem: Kirigami.TitleSubtitle {
                    title: translations.i18n("Use a prepared backup device")
                    subtitle: translations.i18n("Support for assigning an existing encrypted target will be added separately.")
                    selected: false
                }
            }
            QQC2.ItemDelegate {
                Layout.fillWidth: true
                contentItem: Kirigami.TitleSubtitle {
                    title: translations.i18n("Prepare a new backup device")
                    subtitle: translations.i18n("Erase a disk, create LUKS2 and format it as Btrfs")
                    selected: false
                }
                onClicked: {
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

                Kirigami.Heading { text: translations.i18n("Select a disk"); level: 2 }
                Repeater {
                    model: root.provisioning.devices
                    QQC2.ItemDelegate {
                        id: deviceRow
                        required property var modelData
                        Layout.fillWidth: true
                        enabled: !modelData.mounted
                        highlighted: root.selectedDevice?.path === modelData.path
                        onClicked: root.selectedDevice = modelData
                        contentItem: Kirigami.TitleSubtitle {
                            title: (deviceRow.modelData.model || deviceRow.modelData.path)
                                + " - " + root.formatBytes(deviceRow.modelData.sizeBytes)
                            subtitle: deviceRow.modelData.path + " - "
                                + (deviceRow.modelData.mounted
                                    ? translations.i18n("in use")
                                    : deviceRow.modelData.containsData
                                        ? translations.i18n("contains data")
                                        : translations.i18n("empty"))
                            selected: deviceRow.highlighted
                        }
                    }
                }

                Kirigami.Separator { Layout.fillWidth: true }
                GridLayout {
                    Layout.fillWidth: true
                    columns: width > Kirigami.Units.gridUnit * 28 ? 2 : 1
                    columnSpacing: Kirigami.Units.largeSpacing
                    QQC2.TextField {
                        id: profileName
                        Layout.fillWidth: true
                        placeholderText: translations.i18n("Profile name")
                        onTextEdited: if (!profileId.modified) profileId.text = root.slug(text)
                    }
                    QQC2.TextField {
                        id: profileId
                        property bool modified: false
                        Layout.fillWidth: true
                        placeholderText: translations.i18n("Profile identifier")
                        validator: RegularExpressionValidator { regularExpression: /^[a-z0-9][a-z0-9-]{0,62}$/ }
                        onTextEdited: modified = true
                    }
                    QQC2.ComboBox {
                        id: sourcePath
                        Layout.fillWidth: true
                        model: root.provisioning.sourceCandidates
                        editable: false
                        displayText: currentIndex >= 0 ? currentText : translations.i18n("Select source Btrfs subvolume")
                    }
                    QQC2.CheckBox {
                        id: automaticKey
                        Layout.fillWidth: true
                        checked: true
                        text: translations.i18n("Create a protected key for automatic backups")
                    }
                    QQC2.TextField {
                        id: passphrase
                        Layout.fillWidth: true
                        placeholderText: translations.i18n("Recovery passphrase")
                        echoMode: TextInput.Password
                    }
                    QQC2.TextField {
                        id: confirmation
                        Layout.fillWidth: true
                        placeholderText: translations.i18n("Confirm recovery passphrase")
                        echoMode: TextInput.Password
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: root.selectedDevice !== null
                    type: Kirigami.MessageType.Warning
                    text: translations.i18n("All data on %1 will be permanently erased. Type ERASE to confirm.", root.selectedDevice?.path ?? "")
                }
                QQC2.TextField {
                    id: eraseConfirmation
                    Layout.fillWidth: true
                    visible: root.selectedDevice !== null
                    placeholderText: "ERASE"
                }
            }
        }

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing
            Item { Layout.fillHeight: true }
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
                    ? translations.i18n("Device preparation failed. The disk may require manual recovery.")
                    : translations.i18n("Do not disconnect the device while preparation is in progress.")
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
    function formatBytes(value) {
        const gib = Number(value) / 1073741824
        return translations.i18nc("disk size in gibibytes", "%1 GiB", Math.round(gib * 10) / 10)
    }
    function phaseText(phase) {
        switch (phase) {
        case "inspect": return translations.i18n("Inspecting device")
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
