// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

Item {
    id: root
    required property var editor
    required property string profileId
    property var credentialModel: null
    property var credentialToRemove: null
    property var credentialToInspect: null

    implicitHeight: Math.ceil(methodList.count > 0
        ? methodList.contentHeight
        : (methodList.headerItem?.implicitHeight ?? 0) + methodList.emptyContentHeight)
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    Rectangle { anchors.fill: parent; color: Kirigami.Theme.backgroundColor }

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    ListView {
        id: methodList

        readonly property real emptyContentHeight:
            Kirigami.Units.largeSpacing * 4 + emptyMessage.implicitHeight

        anchors.fill: parent
        model: root.credentialModel?.credentials ?? []
        interactive: false
        boundsBehavior: Flickable.StopAtBounds

        header: Kirigami.InlineViewHeader {
            width: methodList.width
            text: translations.i18n("Unlocking methods")
            actions: [
                Kirigami.Action {
                    icon.name: "list-add-symbolic"
                    text: translations.i18n("Add unlocking method")
                    enabled: root.credentialModel !== null && !root.credentialModel.busy
                    onTriggered: addMenu.popup()
                }
            ]

            QQC2.Menu {
                id: addMenu
                QQC2.MenuItem {
                    text: translations.i18n("Add passphrase")
                    icon.name: "dialog-password-symbolic"
                    onTriggered: passphraseDialog.open()
                }
                QQC2.MenuItem {
                    text: translations.i18n("Generate key")
                    icon.name: "password-generate-symbolic"
                    onTriggered: keyDialog.open()
                }
                QQC2.MenuItem {
                    text: translations.i18n("Add key file")
                    icon.name: "document-open-symbolic"
                    onTriggered: keyFileDialog.open()
                }
            }
        }

        delegate: QQC2.ItemDelegate {
            id: methodRow
            objectName: "unlockingMethodRow"
            required property var modelData
            width: ListView.view?.width ?? implicitWidth
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Kirigami.Theme.useAlternateBackgroundColor: true
            Accessible.name: methodDetails.title
            Accessible.description: methodDetails.subtitle
            onClicked: {
                root.credentialToInspect = root.credentialSnapshot(methodRow.modelData);
                methodDetailsDialog.open();
            }

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: root.methodIcon(methodRow.modelData)
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: implicitWidth
                }

                Kirigami.TitleSubtitleWithActions {
                    id: methodDetails
                    objectName: "unlockingMethodDetails"
                    Layout.fillWidth: true
                    title: methodRow.modelData.managed && methodRow.modelData.label
                        ? methodRow.modelData.label
                        : translations.i18n("LUKS key slot %1", methodRow.modelData.keyslot)
                    subtitle: root.methodDescription(methodRow.modelData)
                    selected: false
                    actions: [
                        Kirigami.Action {
                            icon.name: "edit-delete-symbolic"
                            text: translations.i18n("Remove unlocking method")
                            visible: methodRow.modelData.managed && !methodRow.modelData.automatic
                            enabled: (root.credentialModel?.credentials.length ?? 0) > 1 && !root.credentialModel.busy
                            onTriggered: {
                                root.credentialToRemove = methodRow.modelData;
                                removeDialog.open();
                            }
                        }
                    ]
                }
            }
        }

        footer: ColumnLayout {
            width: methodList.width
            spacing: 0

            Item {
                objectName: "unlockingMethodsProgress"
                Layout.fillWidth: true
                implicitHeight: progressRow.implicitHeight + Kirigami.Units.largeSpacing * 2
                visible: root.credentialModel?.busy ?? false

                RowLayout {
                    id: progressRow
                    anchors.centerIn: parent
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.BusyIndicator {
                        objectName: "unlockingMethodsBusyIndicator"
                        running: parent.parent.visible
                        implicitWidth: Kirigami.Units.iconSizes.smallMedium
                        implicitHeight: implicitWidth
                    }
                    QQC2.Label {
                        objectName: "unlockingMethodsProgressText"
                        text: translations.i18n("Updating unlocking methods…")
                    }
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: methodList.count > 0 && (root.credentialModel?.errorMessage?.length ?? 0) > 0
                type: Kirigami.MessageType.Error
                text: root.credentialModel?.errorMessage ?? ""
                showCloseButton: true
                onVisibleChanged: if (!visible) root.credentialModel?.clearError()
            }
        }

        Kirigami.PlaceholderMessage {
            id: emptyMessage
            objectName: "emptyUnlockingMessage"
            width: parent.width - Kirigami.Units.largeSpacing * 4
            anchors.horizontalCenter: parent.horizontalCenter
            y: (methodList.headerItem?.height ?? 0) + Kirigami.Units.largeSpacing * 2
            visible: methodList.count === 0 && !(root.credentialModel?.busy ?? false)
            icon.name: (root.credentialModel?.errorMessage?.length ?? 0) > 0
                ? "dialog-error-symbolic"
                : "lock-symbolic"
            text: (root.credentialModel?.errorMessage?.length ?? 0) > 0
                ? root.credentialModel.errorMessage
                : translations.i18n("No LUKS unlocking methods found")
        }
    }

    QQC2.Dialog {
        id: methodDetailsDialog
        objectName: "unlockingMethodDetailsDialog"
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: root.credentialTitle(root.credentialToInspect)
        standardButtons: QQC2.Dialog.Close

        contentItem: GridLayout {
            width: Kirigami.Units.gridUnit * 24
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.smallSpacing

            QQC2.Label { text: translations.i18n("Type:"); opacity: 0.65 }
            QQC2.Label {
                objectName: "unlockingMethodTypeValue"
                Layout.fillWidth: true
                text: root.methodType(root.credentialToInspect)
            }
            QQC2.Label { text: translations.i18n("LUKS key slot:"); opacity: 0.65 }
            QQC2.Label {
                objectName: "unlockingMethodSlotValue"
                text: root.credentialToInspect?.keyslot ?? translations.i18n("Unknown")
            }
            QQC2.Label { text: translations.i18n("Management:"); opacity: 0.65 }
            QQC2.Label {
                objectName: "unlockingMethodManagementValue"
                Layout.fillWidth: true
                text: root.credentialToInspect?.managed
                    ? translations.i18n("Managed by btrfs-backup")
                    : translations.i18n("Configured outside btrfs-backup")
            }
            QQC2.Label { text: translations.i18n("Usage:"); opacity: 0.65 }
            QQC2.Label {
                objectName: "unlockingMethodUsageValue"
                Layout.fillWidth: true
                text: root.credentialToInspect?.automatic
                    ? translations.i18n("Automatic backups")
                    : translations.i18n("Manual unlocking")
            }
            Kirigami.InlineMessage {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                visible: true
                type: Kirigami.MessageType.Information
                text: root.methodPrivacyText(root.credentialToInspect)
            }
        }
    }

    QQC2.Dialog {
        id: passphraseDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: translations.i18n("Add passphrase")
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        onOpened: {
            passphraseLabel.text = translations.i18n("Additional passphrase");
            currentPassphrase.text = "";
            newPassphrase.text = "";
            confirmPassphrase.text = "";
        }
        onAccepted: root.credentialModel.addPassphrase(currentPassphrase.text, newPassphrase.text, confirmPassphrase.text, passphraseLabel.text)

        contentItem: ColumnLayout {
            width: Kirigami.Units.gridUnit * 24
            spacing: Kirigami.Units.smallSpacing
            QQC2.Label { text: translations.i18n("Name") }
            QQC2.TextField { id: passphraseLabel; Layout.fillWidth: true }
            QQC2.Label { text: translations.i18n("Current passphrase") }
            QQC2.TextField { id: currentPassphrase; Layout.fillWidth: true; echoMode: TextInput.Password }
            QQC2.Label { text: translations.i18n("New passphrase") }
            QQC2.TextField { id: newPassphrase; Layout.fillWidth: true; echoMode: TextInput.Password }
            QQC2.Label { text: translations.i18n("Confirm new passphrase") }
            QQC2.TextField { id: confirmPassphrase; Layout.fillWidth: true; echoMode: TextInput.Password }
        }
    }

    QQC2.Dialog {
        id: keyDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: translations.i18n("Generate key")
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        onOpened: {
            keyLabel.text = translations.i18n("Automatic backup key");
            keyAuthorization.text = "";
            automaticKey.checked = true;
        }
        onAccepted: root.credentialModel.generateKey(keyAuthorization.text, keyLabel.text, automaticKey.checked)

        contentItem: ColumnLayout {
            width: Kirigami.Units.gridUnit * 24
            spacing: Kirigami.Units.smallSpacing
            QQC2.Label { text: translations.i18n("Name") }
            QQC2.TextField { id: keyLabel; Layout.fillWidth: true }
            QQC2.Label { text: translations.i18n("Current passphrase") }
            QQC2.TextField { id: keyAuthorization; Layout.fillWidth: true; echoMode: TextInput.Password }
            QQC2.CheckBox {
                id: automaticKey
                text: translations.i18n("Use this key for automatic backups")
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: translations.i18n("The generated key is stored in the protected system configuration and is never shown in this interface.")
                wrapMode: Text.Wrap
                opacity: 0.7
            }
        }
    }

    QQC2.Dialog {
        id: removeDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: translations.i18n("Remove unlocking method")
        standardButtons: QQC2.Dialog.Yes | QQC2.Dialog.Cancel
        onOpened: removalAuthorization.text = ""
        onAccepted: root.credentialModel.removeCredential(root.credentialToRemove.id, removalAuthorization.text)
        contentItem: ColumnLayout {
            width: Kirigami.Units.gridUnit * 24
            QQC2.Label {
                Layout.fillWidth: true
                text: translations.i18n("Enter an existing passphrase to remove %1.", root.credentialToRemove?.label ?? "")
                wrapMode: Text.Wrap
            }
            QQC2.TextField {
                id: removalAuthorization
                Layout.fillWidth: true
                placeholderText: translations.i18n("Existing passphrase")
                echoMode: TextInput.Password
            }
        }
    }

    FileDialog {
        id: keyFileDialog
        title: translations.i18n("Select key file")
        fileMode: FileDialog.OpenFile
        onAccepted: importedKeyDialog.open()
    }

    QQC2.Dialog {
        id: importedKeyDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: translations.i18n("Add key file")
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        onOpened: {
            importedKeyLabel.text = translations.i18n("Imported key");
            importedKeyAuthorization.text = "";
            importedKeyAutomatic.checked = false;
        }
        onAccepted: root.credentialModel.addKey(
            importedKeyAuthorization.text, keyFileDialog.selectedFile,
            importedKeyLabel.text, importedKeyAutomatic.checked
        )
        contentItem: ColumnLayout {
            width: Kirigami.Units.gridUnit * 24
            spacing: Kirigami.Units.smallSpacing
            QQC2.Label { text: translations.i18n("Name") }
            QQC2.TextField { id: importedKeyLabel; Layout.fillWidth: true }
            QQC2.Label { text: translations.i18n("Current passphrase") }
            QQC2.TextField { id: importedKeyAuthorization; Layout.fillWidth: true; echoMode: TextInput.Password }
            QQC2.CheckBox {
                id: importedKeyAutomatic
                text: translations.i18n("Use this key for automatic backups")
            }
        }
    }

    Component.onCompleted: {
        if (root.credentialModel !== null && root.profileId.length > 0)
            root.credentialModel.load(root.profileId)
    }

    function methodDescription(method) {
        const kind = root.methodType(method)
        const ownership = method.managed ? translations.i18n("managed by btrfs-backup") : translations.i18n("configured outside btrfs-backup")
        const automatic = method.automatic ? translations.i18n("automatic backups") : translations.i18n("manual unlocking")
        return translations.i18nc("unlocking method details", "%1 - slot %2 - %3 - %4", kind, method.keyslot, ownership, automatic)
    }

    function credentialSnapshot(method) {
        return {
            "id": method.id,
            "label": method.label,
            "type": method.type,
            "keyslot": method.keyslot,
            "managed": method.managed,
            "automatic": method.automatic
        }
    }

    function credentialTitle(method) {
        if (method === null || method === undefined)
            return translations.i18n("Unlocking method details")
        return method.managed && method.label
            ? method.label
            : translations.i18n("LUKS key slot %1", method.keyslot)
    }

    function methodType(method) {
        if (method === null || method === undefined)
            return translations.i18n("Unknown unlocking method")
        if (method.type === "passphrase")
            return translations.i18n("Passphrase")
        if (method.type === "keyFile")
            return translations.i18n("Key file")
        return translations.i18n("Unknown unlocking method")
    }

    function methodIcon(method) {
        if (method?.type === "passphrase")
            return "dialog-password";
        if (method?.type === "keyFile" && method?.automatic)
            return "password-generate";
        if (method?.type === "keyFile")
            return "document-encrypt";
        return "dialog-question";
    }

    function methodPrivacyText(method) {
        if (method?.type === "passphrase")
            return translations.i18n("The passphrase itself is not stored by btrfs-backup.")
        if (method?.type === "keyFile" && method.managed)
            return translations.i18n("The key is stored in protected system configuration. Its contents and path are hidden.")
        return translations.i18n("This LUKS slot was configured outside btrfs-backup.")
    }
}
