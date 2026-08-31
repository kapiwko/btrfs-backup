// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
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

    SectionHeading { text: translations.i18n("Unlocking") }

    RowLayout {
        Layout.fillWidth: true

        Kirigami.Icon {
            source: "lock-symbolic"
            implicitWidth: Kirigami.Units.iconSizes.smallMedium
            implicitHeight: implicitWidth
        }
        Kirigami.TitleSubtitle {
            Layout.fillWidth: true
            title: root.activationTitle(root.editor?.target?.activation?.mode ?? "")
            subtitle: translations.i18n("Automatic activation policy")
            selected: false
        }
    }

    function activationTitle(mode) {
        switch (mode) {
        case "keyFile": return translations.i18n("Key file")
        case "askPassword": return translations.i18n("Ask for a password")
        default: return translations.i18n("Unknown unlock method")
        }
    }

    component SectionHeading: RowLayout {
        required property string text
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading { text: parent.text; level: 3 }
        Kirigami.Separator { Layout.fillWidth: true }
    }
}
