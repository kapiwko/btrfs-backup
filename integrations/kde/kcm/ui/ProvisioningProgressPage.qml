// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root

    required property var workflow
    required property var translations

    Layout.fillWidth: true
    Layout.fillHeight: true

    ColumnLayout {
        objectName: "provisioningProgressContent"
        anchors.centerIn: parent
        width: Math.min(
            Math.max(0, parent.width - Kirigami.Units.largeSpacing * 2),
            Kirigami.Units.gridUnit * 36
        )
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Icon {
            Layout.alignment: Qt.AlignHCenter
            source: root.workflow.provisioning.operation.state === "failed"
                ? "dialog-error" : root.workflow.provisioning.operation.phase === "complete"
                    ? "emblem-success" : "drive-removable-media"
            implicitWidth: Kirigami.Units.iconSizes.huge
            implicitHeight: implicitWidth
        }
        QQC2.BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: root.workflow.provisioning.operation.state === "queued"
                || root.workflow.provisioning.operation.state === "running"
        }
        Kirigami.Heading {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: root.workflow.phaseText(root.workflow.provisioning.operation.phase || "inspect")
            level: 2
        }
        QQC2.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: root.workflow.provisioning.operation.state === "failed"
                ? (root.workflow.adoption
                    ? root.translations.i18n("The existing target could not be assigned. Its data was not modified.")
                    : root.translations.i18n("Device preparation failed. The disk may require manual recovery."))
                : (root.workflow.adoption
                    ? root.translations.i18n("Verifying and adding the existing backup target.")
                    : root.translations.i18n("Do not disconnect the device while preparation is in progress."))
        }
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: Kirigami.Units.gridUnit * 32
            visible: (root.workflow.provisioning.operation.operationId ?? "") !== ""
            Repeater {
                model: root.workflow.operationSteps()
                RowLayout {
                    id: phaseRow
                    required property string modelData
                    Kirigami.Icon {
                        source: root.workflow.phaseCompleted(phaseRow.modelData)
                            ? "emblem-success"
                            : root.workflow.provisioning.operation.state === "failed"
                                && root.workflow.provisioning.operation.phase === phaseRow.modelData
                            ? "dialog-error" : "emblem-pause"
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: implicitWidth
                    }
                    QQC2.Label {
                        text: root.workflow.phaseText(phaseRow.modelData)
                        opacity: root.workflow.phaseCompleted(phaseRow.modelData)
                            || root.workflow.provisioning.operation.phase === phaseRow.modelData ? 1 : 0.65
                    }
                }
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: root.workflow.provisioning.operation.state === "failed"
                    && (root.workflow.provisioning.operation.recoveryAction ?? "") !== ""
                wrapMode: Text.Wrap
                text: root.workflow.provisioning.operation.recoveryAction ?? ""
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: (root.workflow.provisioning.operation.operationId ?? "") !== ""
                text: root.translations.i18n(
                    "Operation identifier: %1",
                    root.workflow.provisioning.operation.operationId ?? ""
                )
                elide: Text.ElideMiddle
            }
            RowLayout {
                visible: root.workflow.provisioning.operation.state === "failed"
                QQC2.Button {
                    text: root.translations.i18n("Copy diagnostic report")
                    icon.name: "edit-copy"
                    onClicked: if (typeof kcm !== "undefined")
                        kcm.copyText(root.workflow.diagnosticReport())
                }
                QQC2.Button {
                    text: root.translations.i18n("Open recovery instructions")
                    icon.name: "help-contents"
                    onClicked: if (typeof kcm !== "undefined") kcm.openRecoveryGuide()
                }
            }
        }
        QQC2.Button {
            Layout.alignment: Qt.AlignHCenter
            visible: root.workflow.provisioning.operation.canCancel ?? false
            text: root.translations.i18n("Cancel")
            onClicked: root.workflow.provisioning.cancel()
        }
    }
}
