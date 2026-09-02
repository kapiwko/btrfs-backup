// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

ColumnLayout {
    id: history

    required property var entries
    required property var summaryForEntry
    required property var relativeTimeFor

    visible: entries.length > 0
    spacing: Kirigami.Units.smallSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "plasma_applet_org.btrfsbackup.plasmoid"
    }

    SectionHeader {
        Layout.fillWidth: true
        text: translations.i18n("Recent backups")
    }

    Repeater {
        model: history.entries

        delegate: RowLayout {
            id: historyRow

            required property var modelData
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: historyRow.modelData.state === "succeeded" ? "emblem-ok"
                    : historyRow.modelData.state === "failed" ? "dialog-error"
                    : historyRow.modelData.state === "cancelled" ? "dialog-cancel"
                    : "dialog-information"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: implicitWidth
            }

            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: history.summaryForEntry(historyRow.modelData)
                elide: Text.ElideRight
                font: Kirigami.Theme.smallFont
            }

            PlasmaComponents3.Label {
                Layout.maximumWidth: Kirigami.Units.gridUnit * 8
                text: history.relativeTimeFor(historyRow.modelData.finishedAt)
                elide: Text.ElideRight
                font: Kirigami.Theme.smallFont
                opacity: 0.7

                PlasmaComponents3.ToolTip {
                    text: historyRow.modelData.finishedAt
                }
            }
        }
    }
}
