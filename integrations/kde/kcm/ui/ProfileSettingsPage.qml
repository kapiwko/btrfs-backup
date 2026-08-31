// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    id: root

    required property var editor
    readonly property bool inputValid: nameField.text.trim().length > 0

    title: translations.i18n("Edit profile")
    enabled: root.editor !== null && !root.editor.busy
    actions: Kirigami.Action {
        icon.name: "document-save-symbolic"
        text: translations.i18n("Save")
        enabled: root.inputValid && root.editor !== null && !root.editor.busy
        onTriggered: root.save()
    }

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    Connections {
        target: root.editor

        function onProfileSaved() {
            if (typeof kcm !== "undefined")
                kcm.pop()
        }
    }

    Keys.onReturnPressed: event => {
        if (root.inputValid && !root.editor.busy) {
            root.save()
            event.accepted = true
        }
    }
    Keys.onEscapePressed: event => {
        if (typeof kcm !== "undefined") {
            kcm.pop()
            event.accepted = true
        }
    }

    Kirigami.FormLayout {
        anchors.fill: parent

        Item { Kirigami.FormData.isSection: true; implicitHeight: Kirigami.Units.smallSpacing }

        QQC2.TextField {
            id: nameField
            Kirigami.FormData.label: translations.i18n("Name:")
            Layout.preferredWidth: Kirigami.Units.gridUnit * 18
            maximumLength: 160
            text: root.editor?.name ?? ""
            placeholderText: translations.i18n("Profile name")
            selectByMouse: true
        }

        QQC2.CheckBox {
            id: dailyLimitCheck
            Kirigami.FormData.label: translations.i18n("Scheduling:")
            text: translations.i18n("Limit automatic backups to once per day")
            checked: root.editor?.settings?.dailyLimit ?? false
        }

        QQC2.CheckBox {
            id: autoEjectCheck
            Kirigami.FormData.label: translations.i18n("After backup:")
            text: translations.i18n("Eject the backup device automatically")
            checked: root.editor?.settings?.autoEject ?? false
        }
    }

    Component.onCompleted: {
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function save() {
        if (!root.inputValid || root.editor === null || root.editor.busy)
            return
        root.editor.updateProfileSettings(
            nameField.text, dailyLimitCheck.checked, autoEjectCheck.checked)
    }
}
