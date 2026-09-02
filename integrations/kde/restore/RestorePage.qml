// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: root

    required property var controller
    property bool destinationDialogEnabled: true
    title: translations.i18n("Restore from backup")

    KI18n.KI18nContext {
        id: translations
        translationDomain: "btrfs-backup-kde-restore"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.Label {
                Kirigami.FormData.label: translations.i18n("Source:")
                text: root.controller.sourceName
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }

            RowLayout {
                Kirigami.FormData.label: translations.i18n("Destination:")
                Layout.fillWidth: true
                QQC2.Button {
                    Layout.fillWidth: true
                    text: root.controller.destination
                    icon.name: "folder-open-symbolic"
                    display: QQC2.AbstractButton.TextBesideIcon
                    enabled: destinationDialogLoader.item !== null
                    Accessible.name: translations.i18n("Choose destination")
                    onClicked: destinationDialogLoader.item.open()
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: root.controller.destination
                }
            }

            QQC2.Label {
                Kirigami.FormData.label: translations.i18n("Metadata:")
                text: translations.i18n("Preserve and verify")
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.controller.planSummary.length > 0
            text: root.controller.planSummary
            type: Kirigami.MessageType.Information
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.controller.errorText.length > 0
            text: root.controller.errorText
            type: Kirigami.MessageType.Error
        }

        QQC2.ProgressBar {
            Layout.fillWidth: true
            visible: root.controller.busy
            indeterminate: true
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            QQC2.Button {
                icon.name: "document-preview"
                text: translations.i18n("Preview plan")
                enabled: !root.controller.busy
                onClicked: root.controller.preview()
            }
            QQC2.Button {
                visible: root.controller.busy
                icon.name: "dialog-cancel"
                text: translations.i18n("Cancel")
                onClicked: root.controller.cancel()
            }
            QQC2.Button {
                visible: !root.controller.busy
                icon.name: "document-restore"
                text: translations.i18n("Restore")
                highlighted: true
                enabled: root.controller.planSummary.length > 0
                onClicked: root.controller.execute()
            }
        }
    }

    Connections {
        target: root.controller

        function onOverwriteConfirmationRequested(destination) {
            overwriteDialog.destination = destination;
            overwriteDialog.open();
        }
    }

    QQC2.Dialog {
        id: overwriteDialog
        property string destination: ""

        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: translations.i18n("Destination already exists")
        standardButtons: QQC2.Dialog.Yes | QQC2.Dialog.No
        onAccepted: root.controller.confirmOverwrite()

        contentItem: Kirigami.InlineMessage {
            type: Kirigami.MessageType.Warning
            visible: true
            text: translations.i18n("%1 already exists. Replace it transactionally?", overwriteDialog.destination)
        }
    }

    Loader {
        id: destinationDialogLoader
        active: root.destinationDialogEnabled
        sourceComponent: FolderDialog {
            currentFolder: "file://" + root.controller.destination.substring(0, root.controller.destination.lastIndexOf("/"))
            onAccepted: root.controller.destination = selectedFolder.toString().replace(/^file:\/\//, "") + "/" + root.controller.sourceName
        }
    }
}
