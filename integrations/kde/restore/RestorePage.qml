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
    signal closeRequested()
    Component.onCompleted: root.controller.loadDetails()

    KI18n.KI18nContext {
        id: translations
        translationDomain: "btrfs-backup-kde-restore"
    }

    ColumnLayout {
        id: restoreForm
        anchors.fill: parent
        spacing: Kirigami.Units.largeSpacing
        visible: !root.controller.completed

        Kirigami.AbstractCard {
            Layout.fillWidth: true

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    objectName: "restoreSourceIcon"
                    source: root.controller.sourceIcon
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: Kirigami.Units.iconSizes.huge
                    implicitHeight: implicitWidth
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        objectName: "restoreSourceName"
                        Layout.fillWidth: true
                        text: root.controller.sourceName
                        elide: Text.ElideMiddle
                        font.weight: Font.DemiBold
                        font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.1
                    }

                    GridLayout {
                        objectName: "restoreSourceDetails"
                        visible: root.controller.sourceDetailsAvailable
                        columns: 2
                        columnSpacing: Kirigami.Units.largeSpacing
                        rowSpacing: Kirigami.Units.smallSpacing

                        QQC2.Label { text: translations.i18n("Type:"); opacity: 0.7 }
                        QQC2.Label { text: root.controller.sourceType }
                        QQC2.Label { text: translations.i18n("Size:"); opacity: 0.7 }
                        QQC2.Label { text: root.controller.sourceSize }
                        QQC2.Label { text: translations.i18n("Modified:"); opacity: 0.7 }
                        QQC2.Label { text: root.controller.sourceModified }
                        QQC2.Label { text: translations.i18n("Backup date:"); opacity: 0.7 }
                        QQC2.Label { text: root.controller.snapshotCreated }
                    }
                }
            }
        }

        QQC2.Label {
            text: translations.i18n("Restore to")
            font.weight: Font.DemiBold
        }

        RowLayout {
            Layout.fillWidth: true

            QQC2.TextField {
                objectName: "restoreDestinationField"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.controller.destination
                readOnly: true
                selectByMouse: true
                Accessible.name: translations.i18n("Restore destination")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: root.controller.destination
            }
            QQC2.Button {
                objectName: "chooseRestoreDestinationButton"
                Layout.fillWidth: false
                icon.name: "folder-open-symbolic"
                text: translations.i18n("Choose…")
                enabled: !root.controller.busy
                Accessible.name: translations.i18n("Choose destination")
                onClicked: root.controller.chooseDestination()
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: translations.i18n("File ownership, permissions and timestamps will be preserved and verified.")
            wrapMode: Text.Wrap
            opacity: 0.75
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

        Kirigami.AbstractCard {
            objectName: "restoreProgressCard"
            Layout.fillWidth: true
            visible: root.controller.busy

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    source: root.controller.sourceIcon
                    implicitWidth: Kirigami.Units.iconSizes.large
                    implicitHeight: implicitWidth
                }

                ColumnLayout {
                    Layout.fillWidth: true

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: translations.i18n("Restoring %1", root.controller.sourceName)
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }
                    QQC2.ProgressBar {
                        objectName: "restoreProgressBar"
                        Layout.fillWidth: true
                        indeterminate: root.controller.progress < 0
                        value: root.controller.progress < 0 ? 0 : root.controller.progress
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        QQC2.Label {
                            objectName: "restoreTransferredSize"
                            text: root.controller.transferredSize
                            opacity: 0.75
                        }
                        Item { Layout.fillWidth: true }
                        QQC2.Label {
                            objectName: "restoreTransferSpeed"
                            text: root.controller.transferSpeed
                            visible: text.length > 0
                            opacity: 0.75
                        }
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: root.controller.currentItem
                        visible: text.length > 0 && text !== root.controller.sourceName
                        elide: Text.ElideMiddle
                        opacity: 0.7
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            QQC2.Button {
                visible: root.controller.busy
                icon.name: "dialog-cancel"
                text: translations.i18n("Cancel")
                onClicked: root.controller.cancel()
            }
            QQC2.Button {
                visible: !root.controller.busy
                icon.name: "document-revert"
                text: translations.i18n("Restore")
                highlighted: true
                enabled: root.controller.sourceDetailsAvailable
                    && root.controller.destination.length > 0
                onClicked: root.controller.execute()
            }
        }
    }

    ColumnLayout {
        id: successView
        objectName: "restoreSuccessView"
        anchors.fill: parent
        spacing: Kirigami.Units.largeSpacing
        visible: root.controller.completed

        Item { Layout.fillHeight: true }

        Kirigami.Icon {
            source: "dialog-positive"
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: Kirigami.Units.iconSizes.huge
            implicitHeight: implicitWidth
        }

        QQC2.Label {
            objectName: "restoreSuccessTitle"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: translations.i18np("Restored one file", "Restored %1 files", root.controller.restoredFiles)
            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.35
            font.weight: Font.DemiBold
        }

        QQC2.Label {
            objectName: "restoreSuccessDetails"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: translations.i18n("%1 restored to %2", root.controller.restoredSize, root.controller.destination)
            wrapMode: Text.Wrap
            opacity: 0.8
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter

            QQC2.Button {
                objectName: "openRestoredLocationButton"
                icon.name: "folder-open-symbolic"
                text: root.controller.sourceIsDirectory
                    ? translations.i18n("Open restored folder")
                    : translations.i18n("Show restored file")
                onClicked: root.controller.openRestoredLocation()
            }
            QQC2.Button {
                objectName: "closeRestoreButton"
                icon.name: "window-close-symbolic"
                text: translations.i18n("Close")
                highlighted: true
                onClicked: root.closeRequested()
            }
        }

        Item { Layout.fillHeight: true }
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
