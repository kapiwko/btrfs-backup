// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

QQC2.Dialog {
    id: root
    objectName: "generateKeyDialog"
    required property var credentialModel
    parent: QQC2.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: translations.i18n("Generate key")
    standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
    onOpened: {
        keyLabel.text = translations.i18n("Automatic backup key")
        keyAuthorization.text = ""
        automaticKey.checked = true
    }
    onAccepted: root.credentialModel.generateKey(
        keyAuthorization.text, keyLabel.text, automaticKey.checked)
    onClosed: keyAuthorization.text = ""

    KI18n.KI18nContext { id: translations; translationDomain: "kcm_btrfsbackup" }

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
