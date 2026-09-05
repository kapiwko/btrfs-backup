// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

QQC2.Dialog {
    id: root

    property int sourceIndex: -1
    property bool editing: sourceIndex >= 0
    property bool candidateOnly: false
    property var sourceCandidates: []
    property string editingSubvolume: ""
    property string editingCandidateId: ""
    readonly property var selectedCandidate: !editing && candidateOnly
        && subvolumeField.currentIndex >= 0
        ? sourceCandidates[subvolumeField.currentIndex] : null
    readonly property string subvolumeText: editing ? editingSubvolume
        : candidateOnly ? (selectedCandidate?.path ?? "")
        : subvolumeField.editable ? subvolumeField.editText : subvolumeField.currentText
    readonly property string candidateId: editing ? editingCandidateId
        : candidateOnly ? (selectedCandidate?.id ?? "") : ""
    readonly property bool inputValid: nameField.text.trim().length > 0
        && (editing || root.subvolumeText.trim().startsWith("/"))
        && (!candidateOnly || candidateId.length > 0)

    signal addAccepted(string name, string subvolume, int localRetention,
        int targetRetention, string candidateId)
    signal editAccepted(int index, string name, int localRetention,
        int targetRetention)

    parent: QQC2.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: editing ? translations.i18n("Edit source") : translations.i18n("Add source")
    standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    onOpened: standardButton(QQC2.Dialog.Ok).enabled = Qt.binding(() => root.inputValid)
    onAccepted: {
        if (editing) {
            root.editAccepted(sourceIndex, nameField.text,
                localRetention.value, targetRetention.value)
        } else {
            root.addAccepted(nameField.text, root.subvolumeText,
                localRetention.value, targetRetention.value, root.candidateId)
        }
    }

    function openForAdd() {
        sourceIndex = -1
        editingSubvolume = ""
        editingCandidateId = ""
        nameField.text = ""
        subvolumeField.currentIndex = candidateOnly && sourceCandidates.length > 0 ? 0 : -1
        subvolumeField.editText = ""
        localRetention.value = 30
        targetRetention.value = 30
        open()
        nameField.forceActiveFocus()
    }

    function openForEdit(index, source) {
        sourceIndex = index
        editingSubvolume = source.subvolume || ""
        editingCandidateId = source.candidateId || ""
        nameField.text = source.name || source.id || ""
        subvolumeField.currentIndex = 0
        subvolumeField.editText = editingSubvolume
        localRetention.value = source.localRetention || 30
        targetRetention.value = source.remoteRetention || 30
        open()
        nameField.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.TextField {
                id: nameField
                Kirigami.FormData.label: translations.i18n("Name:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 22
                maximumLength: 160
                selectByMouse: true
            }
            QQC2.ComboBox {
                id: subvolumeField
                objectName: "subvolumeField"
                Kirigami.FormData.label: translations.i18n("Btrfs subvolume:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 22
                editable: !root.editing && !root.candidateOnly
                enabled: !root.editing
                model: root.editing ? [root.editingSubvolume] : root.sourceCandidates
                textRole: root.candidateOnly && !root.editing ? "displayName" : ""
                valueRole: root.candidateOnly && !root.editing ? "id" : ""
                editText: ""
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            ColumnLayout {
                Layout.fillWidth: true
                QQC2.Label { text: translations.i18n("Local snapshots") }
                QQC2.SpinBox {
                    id: localRetention
                    Layout.fillWidth: true
                    from: 1
                    to: 100000
                    editable: true
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                QQC2.Label { text: translations.i18n("Target snapshots") }
                QQC2.SpinBox {
                    id: targetRetention
                    Layout.fillWidth: true
                    from: 1
                    to: 100000
                    editable: true
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.editing
            type: Kirigami.MessageType.Information
            text: translations.i18n("The subvolume binding cannot be changed. Remove this source and add a new one instead.")
        }
    }
}
