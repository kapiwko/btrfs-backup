// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

QQC2.Dialog {
    id: root
    objectName: "addPassphraseDialog"
    required property var credentialModel
    parent: QQC2.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: translations.i18n("Add passphrase")
    standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
    onOpened: {
        passphraseLabel.text = translations.i18n("Additional passphrase")
        currentPassphrase.text = ""
        newPassphrase.text = ""
        confirmPassphrase.text = ""
    }
    onAccepted: root.credentialModel.addPassphrase(
        currentPassphrase.text, newPassphrase.text, confirmPassphrase.text, passphraseLabel.text)
    onClosed: {
        currentPassphrase.text = ""
        newPassphrase.text = ""
        confirmPassphrase.text = ""
    }

    KI18n.KI18nContext { id: translations; translationDomain: "kcm_btrfsbackup" }

    contentItem: ColumnLayout {
        width: Kirigami.Units.gridUnit * 24
        spacing: Kirigami.Units.smallSpacing
        QQC2.Label { text: translations.i18n("Name") }
        QQC2.TextField { id: passphraseLabel; Layout.fillWidth: true }
        QQC2.Label { text: translations.i18n("Current passphrase") }
        QQC2.TextField { id: currentPassphrase; Layout.fillWidth: true; echoMode: TextInput.Password }
        QQC2.Label { text: translations.i18n("New passphrase") }
        QQC2.TextField { id: newPassphrase; Layout.fillWidth: true; echoMode: TextInput.Password }
        QQC2.Label { text: translations.i18n("Confirm new passphrase") }
        QQC2.TextField { id: confirmPassphrase; Layout.fillWidth: true; echoMode: TextInput.Password }
    }
}
