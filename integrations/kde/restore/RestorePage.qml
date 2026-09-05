// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: root

    required property var controller
    property bool technicalDetailsVisible: false
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
                    Accessible.name: translations.i18n("Choose destination")
                    onClicked: root.controller.chooseDestination()
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

        RowLayout {
            Layout.fillWidth: true
            visible: root.controller.errorCode.length > 0

            QQC2.Label {
                Layout.fillWidth: true
                text: translations.i18n("Code: %1", root.controller.errorCode)
                opacity: 0.75
                selectByMouse: true
            }
            QQC2.Button {
                visible: root.controller.errorTechnicalDetails.length > 0
                text: root.technicalDetailsVisible
                    ? translations.i18n("Hide technical details")
                    : translations.i18n("Show technical details")
                icon.name: root.technicalDetailsVisible ? "arrow-up-symbolic" : "arrow-down-symbolic"
                onClicked: root.technicalDetailsVisible = !root.technicalDetailsVisible
            }
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.maximumHeight: Kirigami.Units.gridUnit * 7
            visible: root.technicalDetailsVisible
                && root.controller.errorTechnicalDetails.length > 0

            QQC2.TextArea {
                text: root.controller.errorTechnicalDetails
                readOnly: true
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                Accessible.name: translations.i18n("Technical error details")
            }
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

}
