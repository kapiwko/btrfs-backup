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
    property var sourceCandidates: []
    property string editingSubvolume: ""
    readonly property string subvolumeText: subvolumeField.editable
        ? subvolumeField.editText : subvolumeField.currentText
    readonly property bool inputValid: nameField.text.trim().length > 0
        && (editing || root.subvolumeText.trim().startsWith("/"))

    signal addAccepted(string name, string subvolume, int localRetention,
        int targetRetention)
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
                localRetention.value, targetRetention.value)
        }
    }

    function openForAdd() {
        sourceIndex = -1
        editingSubvolume = ""
        nameField.text = ""
        subvolumeField.currentIndex = -1
        subvolumeField.editText = ""
        localRetention.value = 30
        targetRetention.value = 30
        open()
        nameField.forceActiveFocus()
    }

    function openForEdit(index, source) {
        sourceIndex = index
        editingSubvolume = source.subvolume || ""
        nameField.text = source.name || source.id || ""
        const value = source.subvolume || ""
        const candidateIndex = root.sourceCandidates.indexOf(value)
        subvolumeField.currentIndex = candidateIndex
        subvolumeField.editText = value
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
                Kirigami.FormData.label: translations.i18n("Btrfs subvolume:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 22
                editable: !root.editing
                enabled: !root.editing
                model: root.editing ? [root.editingSubvolume] : root.sourceCandidates
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
