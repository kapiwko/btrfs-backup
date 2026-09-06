// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

Item {
    id: root
    objectName: "importKeyDialog"
    required property var credentialModel
    visible: false
    implicitWidth: 0
    implicitHeight: 0

    function open() { keyFileDialog.open() }

    KI18n.KI18nContext { id: translations; translationDomain: "kcm_btrfsbackup" }

    FileDialog {
        id: keyFileDialog
        title: translations.i18n("Select key file")
        fileMode: FileDialog.OpenFile
        onAccepted: detailsDialog.open()
    }

    QQC2.Dialog {
        id: detailsDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: translations.i18n("Add key file")
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        onOpened: {
            importedKeyLabel.text = translations.i18n("Imported key")
            importedKeyAuthorization.text = ""
            importedKeyAutomatic.checked = false
        }
        onAccepted: root.credentialModel.addKey(
            importedKeyAuthorization.text, keyFileDialog.selectedFile,
            importedKeyLabel.text, importedKeyAutomatic.checked)
        onClosed: importedKeyAuthorization.text = ""

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
}
