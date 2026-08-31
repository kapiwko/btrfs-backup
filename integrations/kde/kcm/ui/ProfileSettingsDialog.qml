// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

QQC2.Dialog {
    id: root

    required property var editor

    parent: QQC2.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: translations.i18n("Edit profile settings")
    standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
    enabled: root.editor !== null && !root.editor.busy

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    onAccepted: root.editor.updateProfileSettings(
        nameField.text,
        dailyLimitCheck.checked,
        autoEjectCheck.checked)

    contentItem: Kirigami.FormLayout {
        QQC2.TextField {
            id: nameField
            Kirigami.FormData.label: translations.i18n("Name:")
            Layout.preferredWidth: Kirigami.Units.gridUnit * 18
            maximumLength: 160
            placeholderText: translations.i18n("Profile name")
        }

        QQC2.CheckBox {
            id: dailyLimitCheck
            Kirigami.FormData.label: translations.i18n("Scheduling:")
            text: translations.i18n("Limit automatic backups to once per day")
        }

        QQC2.CheckBox {
            id: autoEjectCheck
            Kirigami.FormData.label: translations.i18n("After backup:")
            text: translations.i18n("Eject the backup device automatically")
        }
    }

    function openForProfile() {
        if (root.editor === null || !root.editor.loaded)
            return
        nameField.text = root.editor.name
        dailyLimitCheck.checked = root.editor.settings.dailyLimit ?? false
        autoEjectCheck.checked = root.editor.settings.autoEject ?? false
        open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }
}
