// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    required property bool running
    required property string activityText
    required property string sourceName
    required property string progressText
    required property string speedText
    required property string etaText

    visible: running
    spacing: Kirigami.Units.largeSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "plasma_applet_org.btrfsbackup.plasmoid"
    }

    Kirigami.Separator { Layout.fillWidth: true }

    Kirigami.Heading {
        text: translations.i18n("Current activity")
        level: 3
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true

        QQC2.Label {
            Kirigami.FormData.label: translations.i18n("Status:")
            text: root.activityText
            Layout.fillWidth: true
            elide: Text.ElideRight
        }

        QQC2.Label {
            Kirigami.FormData.label: translations.i18n("Source:")
            text: root.sourceName
            Layout.fillWidth: true
            elide: Text.ElideMiddle
        }

        QQC2.Label {
            Kirigami.FormData.label: translations.i18n("Progress:")
            text: root.progressText
        }

        QQC2.Label {
            Kirigami.FormData.label: translations.i18n("Speed:")
            text: root.speedText
        }

        QQC2.Label {
            Kirigami.FormData.label: translations.i18n("Time remaining:")
            text: root.etaText
        }
    }
}
