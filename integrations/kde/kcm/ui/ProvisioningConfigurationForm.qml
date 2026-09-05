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
    readonly property alias profileName: profileNameField.text
    readonly property alias profileIdentifier: profileIdentifierField.text
    readonly property alias profileIdentifierAcceptable: profileIdentifierField.acceptableInput
    readonly property alias sourceCurrentIndex: sourcePathField.currentIndex
    readonly property alias sourceCurrentValue: sourcePathField.currentValue
    readonly property alias passphrase: passphraseField.text
    readonly property alias confirmation: confirmationField.text
    readonly property alias automaticKey: automaticKeyField.checked
    readonly property alias eraseConfirmation: eraseConfirmationField.text
    readonly property real preferredFormWidth: Kirigami.Units.gridUnit * 32

    Layout.fillWidth: true
    spacing: Kirigami.Units.largeSpacing

    Kirigami.Separator { Layout.fillWidth: true }
    Kirigami.Heading {
        text: root.translations.i18n("Profile details")
        level: 2
    }
    Kirigami.FormLayout {
        id: profileDetailsForm

        objectName: "profileDetailsForm"
        Layout.fillWidth: true
        Layout.maximumWidth: root.preferredFormWidth
        QQC2.TextField {
            id: profileNameField
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Kirigami.FormData.label: root.translations.i18n("Profile name:")
            placeholderText: root.translations.i18n("Profile name")
            maximumLength: 160
            onTextEdited: if (!profileIdentifierField.modified)
                profileIdentifierField.text = root.workflow.slug(text)
        }
        QQC2.Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            wrapMode: Text.Wrap
            text: root.translations.i18n("The name shown in the application. It can contain spaces and can be changed later.")
            opacity: 0.75
        }
        QQC2.TextField {
            id: profileIdentifierField
            property bool modified: false
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Kirigami.FormData.label: root.translations.i18n("Profile identifier:")
            placeholderText: root.translations.i18n("Profile identifier")
            maximumLength: 63
            validator: RegularExpressionValidator { regularExpression: /^[a-z0-9][a-z0-9-]{0,62}$/ }
            onTextEdited: modified = true
        }
        QQC2.Label {
            id: profileIdentifierHelp

            objectName: "profileIdentifierHelp"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            wrapMode: Text.Wrap
            text: root.translations.i18n("A permanent technical name used in configuration. Enter 1–63 lowercase letters, digits, or hyphens; start with a letter or digit.")
            opacity: 0.75
        }
        QQC2.ComboBox {
            id: sourcePathField
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Kirigami.FormData.label: root.translations.i18n("Backup source:")
            model: root.workflow.provisioning.sourceCandidates
            textRole: "displayName"
            valueRole: "id"
            editable: false
            displayText: currentIndex >= 0
                ? currentText : root.translations.i18n("Select source Btrfs subvolume")
        }
        QQC2.Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            wrapMode: Text.Wrap
            text: root.translations.i18n("The Btrfs subvolume whose data will be included in this backup profile.")
            opacity: 0.75
        }
        QQC2.Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Kirigami.FormData.label: root.translations.i18n("Local snapshots:")
            text: root.workflow.localSnapshotDirectory
            visible: text.length > 0
            elide: Text.ElideMiddle
        }
        QQC2.Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: root.workflow.localSnapshotDirectory.length > 0
            wrapMode: Text.Wrap
            text: root.translations.i18n("Temporary local snapshots are created here before they are copied to the backup device.")
            opacity: 0.75
        }
    }

    Kirigami.Heading {
        text: root.translations.i18n("Encryption")
        level: 2
    }
    Kirigami.FormLayout {
        id: encryptionForm

        objectName: "encryptionForm"
        Layout.fillWidth: true
        Layout.maximumWidth: root.preferredFormWidth
        QQC2.CheckBox {
            id: automaticKeyField

            objectName: "automaticKeyField"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Kirigami.FormData.label: root.translations.i18n("Automatic backups:")
            checked: true
            visible: !root.workflow.adoption
            text: root.translations.i18n("Create a protected key for automatic backups")
        }
        QQC2.Label {
            id: automaticKeyExplanation

            objectName: "automaticKeyExplanation"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: automaticKeyField.visible && automaticKeyField.checked
            wrapMode: Text.Wrap
            text: root.translations.i18n("The key is stored in a root-only system directory and allows backups to unlock the target without a prompt. The recovery passphrase remains available if the key is lost or removed.")
            opacity: 0.75
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Kirigami.FormData.label: root.workflow.adoption
                ? root.translations.i18n("Existing passphrase:")
                : root.translations.i18n("Recovery passphrase:")
            QQC2.TextField {
                id: passphraseField

                objectName: "passphraseField"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                placeholderText: root.workflow.adoption
                    ? root.translations.i18n("Existing target passphrase")
                    : root.translations.i18n("Recovery passphrase")
                echoMode: root.workflow.passwordVisible ? TextInput.Normal : TextInput.Password
            }
            QQC2.ToolButton {
                icon.name: root.workflow.passwordVisible
                    ? "view-hidden-symbolic" : "view-visible-symbolic"
                text: root.workflow.passwordVisible
                    ? root.translations.i18n("Hide password")
                    : root.translations.i18n("Show password")
                display: QQC2.AbstractButton.IconOnly
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: text
                onClicked: root.workflow.passwordVisible = !root.workflow.passwordVisible
            }
        }
        QQC2.TextField {
            id: confirmationField

            objectName: "confirmationField"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Kirigami.FormData.label: root.translations.i18n("Confirm passphrase:")
            visible: !root.workflow.adoption
            placeholderText: root.translations.i18n("Confirm recovery passphrase")
            echoMode: root.workflow.passwordVisible ? TextInput.Normal : TextInput.Password
        }
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: !root.workflow.adoption && confirmationField.text.length > 0
            && passphraseField.text !== confirmationField.text
        type: Kirigami.MessageType.Error
        text: root.translations.i18n("The passphrases do not match.")
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: root.workflow.selectedTarget !== null
        type: root.workflow.adoption
            && (root.workflow.inspectionClassification === "" || root.workflow.hasPlan)
            ? Kirigami.MessageType.Information : Kirigami.MessageType.Warning
        text: root.workflow.adoption
            ? (root.workflow.inspectionClassification === "empty-filesystem"
                ? root.translations.i18n("This encrypted Btrfs filesystem is empty and does not contain a backup repository.")
                : root.workflow.inspectionClassification === "legacy-repository"
                ? root.translations.i18n("This target uses an older backup layout and cannot be adopted automatically.")
                : root.workflow.inspectionClassification === "unsupported-repository"
                ? root.translations.i18n("This repository format is not supported by this version of btrfs-backup.")
                : root.workflow.inspectionClassification === "foreign-or-invalid-repository"
                ? root.translations.i18n("This target does not contain a valid btrfs-backup repository.")
                : root.workflow.inspectionClassification === "not-btrfs-filesystem"
                ? root.translations.i18n("The encrypted target does not contain a Btrfs filesystem.")
                : root.workflow.provisioning.inspection.repositoryId
                ? root.translations.i18np(
                    "Repository %2 contains %1 snapshot. No data will be modified.",
                    "Repository %2 contains %1 snapshots. No data will be modified.",
                    Number(root.workflow.provisioning.inspection.snapshotCount),
                    root.workflow.provisioning.inspection.repositoryId
                )
                : root.translations.i18n("The target will be opened read-only and verified before it can be assigned."))
            : root.workflow.provisioning.plan.mode === "reformat-existing-partition"
            ? root.translations.i18n(
                "All data on partition %1 will be permanently erased. Other partitions will remain unchanged. Type %2 to confirm.",
                root.workflow.selectedTarget?.partitionNumber ?? "",
                root.workflow.confirmationToken
            )
            : root.workflow.freeSpace
            ? root.translations.i18n("A new partition will be created in the selected free space. Existing partitions will remain unchanged. Type CREATE to confirm.")
            : root.translations.i18n(
                "All data on %1 will be permanently erased. Type %2 to confirm.",
                root.translations.i18n("the selected storage device"),
                root.workflow.confirmationToken
            )
    }
    QQC2.TextField {
        id: eraseConfirmationField
        Layout.fillWidth: true
        visible: root.workflow.selectedDevice !== null && !root.workflow.adoption
        Accessible.name: root.translations.i18n("Destructive operation confirmation")
        placeholderText: root.workflow.confirmationToken
    }
}
