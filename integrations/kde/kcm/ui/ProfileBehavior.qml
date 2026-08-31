// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    required property var editor
    spacing: Kirigami.Units.largeSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    SectionHeading { text: translations.i18n("Backup behavior") }

    Kirigami.FormLayout {
        Layout.fillWidth: true

        QQC2.Label {
            Kirigami.FormData.label: translations.i18n("Daily limit:")
            text: root.enabledText(root.editor?.settings?.dailyLimit ?? false)
        }
        QQC2.Label {
            Kirigami.FormData.label: translations.i18n("Automatic eject:")
            text: root.enabledText(root.editor?.settings?.autoEject ?? false)
        }
    }

    function enabledText(value) {
        return value ? translations.i18n("Enabled") : translations.i18n("Disabled")
    }

    component SectionHeading: RowLayout {
        required property string text
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading { text: parent.text; level: 3 }
        Kirigami.Separator { Layout.fillWidth: true }
    }
}
