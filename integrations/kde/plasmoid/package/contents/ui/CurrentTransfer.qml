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
    id: transfer

    required property bool active
    required property int progress
    required property string activityLabel
    required property string sourceLabel
    required property real currentSpeed
    required property string currentSpeedText
    required property string etaLabel
    required property string durationLabel

    visible: active
    spacing: Kirigami.Units.smallSpacing * 2

    KI18n.KI18nContext {
        id: translations
        translationDomain: "plasma_applet_org.btrfsbackup.plasmoid"
    }

    RowLayout {
        Layout.fillWidth: true

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: transfer.activityLabel
                elide: Text.ElideRight
                font.weight: Font.DemiBold
            }

            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: transfer.sourceLabel
                elide: Text.ElideMiddle
                font: Kirigami.Theme.smallFont
                opacity: 0.72
            }
        }

        PlasmaComponents3.Label {
            visible: transfer.progress >= 0
            text: transfer.progress + "%"
            font.weight: Font.DemiBold
        }
    }

    PlasmaComponents3.ProgressBar {
        Layout.fillWidth: true
        from: 0
        to: 100
        value: Math.max(0, transfer.progress)
        indeterminate: transfer.progress < 0
    }

    TransferRate {
        Layout.fillWidth: true
        active: transfer.active
        currentSpeed: transfer.currentSpeed
        currentSpeedText: transfer.currentSpeedText
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        rowSpacing: Kirigami.Units.smallSpacing / 4
        columnSpacing: Kirigami.Units.smallSpacing

        PlasmaComponents3.Label {
            text: translations.i18n("Time remaining:")
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.smallFont
            opacity: 0.6
        }
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: transfer.etaLabel
            font: Kirigami.Theme.smallFont
        }

        PlasmaComponents3.Label {
            text: translations.i18n("Duration:")
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.smallFont
            opacity: 0.6
        }
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: transfer.durationLabel
            font: Kirigami.Theme.smallFont
        }
    }
}
