// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

QQC2.Dialog {
    id: root
    objectName: "removeCredentialDialog"
    required property var credentialModel
    property string credentialId: ""
    property string credentialLabel: ""
    parent: QQC2.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: translations.i18n("Remove unlocking method")
    standardButtons: QQC2.Dialog.Yes | QQC2.Dialog.Cancel
    onOpened: removalAuthorization.text = ""
    onAccepted: root.credentialModel.removeCredential(root.credentialId, removalAuthorization.text)
    onClosed: removalAuthorization.text = ""

    KI18n.KI18nContext { id: translations; translationDomain: "kcm_btrfsbackup" }

    contentItem: ColumnLayout {
        width: Kirigami.Units.gridUnit * 24
        QQC2.Label {
            Layout.fillWidth: true
            text: translations.i18n("Enter an existing passphrase to remove %1.", root.credentialLabel)
            wrapMode: Text.Wrap
        }
        QQC2.TextField {
            id: removalAuthorization
            Layout.fillWidth: true
            placeholderText: translations.i18n("Existing passphrase")
            echoMode: TextInput.Password
        }
    }
}
