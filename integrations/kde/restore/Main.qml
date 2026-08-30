// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    width: 620
    height: 430
    minimumWidth: 420
    minimumHeight: 360
    title: i18n("Restore %1", restoreController.sourceName)
    visible: true

    pageStack.initialPage: Kirigami.Page {
        title: i18n("Restore from backup")

        ColumnLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.largeSpacing

            Kirigami.FormLayout {
                Layout.fillWidth: true

                QQC2.Label {
                    Kirigami.FormData.label: i18n("Source:")
                    text: restoreController.sourceName
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Destination:")
                    Layout.fillWidth: true
                    QQC2.TextField {
                        Layout.fillWidth: true
                        text: restoreController.destination
                        onEditingFinished: restoreController.destination = text
                    }
                    QQC2.ToolButton {
                        icon.name: "folder-open-symbolic"
                        text: i18n("Choose destination")
                        display: QQC2.AbstractButton.IconOnly
                        onClicked: destinationDialog.open()
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.text: text
                    }
                }

                QQC2.ComboBox {
                    Kirigami.FormData.label: i18n("If destination exists:")
                    model: [i18n("Stop"), i18n("Replace transactionally")]
                    currentIndex: restoreController.replaceExisting ? 1 : 0
                    onActivated: restoreController.replaceExisting = currentIndex === 1
                }

                QQC2.Label {
                    Kirigami.FormData.label: i18n("Metadata:")
                    text: i18n("Preserve and verify")
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: restoreController.planSummary.length > 0
                text: restoreController.planSummary
                type: Kirigami.MessageType.Information
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: restoreController.errorText.length > 0
                text: restoreController.errorText
                type: Kirigami.MessageType.Error
            }

            QQC2.ProgressBar {
                Layout.fillWidth: true
                visible: restoreController.busy
                indeterminate: true
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                QQC2.Button {
                    icon.name: "document-preview"
                    text: i18n("Preview plan")
                    enabled: !restoreController.busy
                    onClicked: restoreController.preview()
                }
                QQC2.Button {
                    visible: restoreController.busy
                    icon.name: "dialog-cancel"
                    text: i18n("Cancel")
                    onClicked: restoreController.cancel()
                }
                QQC2.Button {
                    visible: !restoreController.busy
                    icon.name: "document-restore"
                    text: i18n("Restore")
                    highlighted: true
                    enabled: restoreController.planSummary.length > 0
                    onClicked: restoreController.execute()
                }
            }
        }
    }

    FolderDialog {
        id: destinationDialog
        currentFolder: "file://" + restoreController.destination.substring(0, restoreController.destination.lastIndexOf("/"))
        onAccepted: restoreController.destination = selectedFolder.toString().replace(/^file:\/\//, "") + "/" + restoreController.sourceName
    }
}
