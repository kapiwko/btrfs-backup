// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: rate

    required property bool active
    required property real currentSpeed
    required property string currentSpeedText
    property real peakSpeed: 0
    property string peakSpeedText: currentSpeedText

    spacing: Kirigami.Units.smallSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "plasma_applet_org.btrfsbackup.plasmoid"
    }

    function resetPeak() {
        peakSpeed = 0
        peakSpeedText = currentSpeedText
        updatePeak()
    }

    function updatePeak() {
        if (!active)
            return
        const value = Math.max(0, Number(currentSpeed || 0))
        if (value >= peakSpeed) {
            peakSpeed = value
            peakSpeedText = currentSpeedText
        }
    }

    onActiveChanged: {
        if (active)
            resetPeak()
    }
    onCurrentSpeedChanged: updatePeak()
    onCurrentSpeedTextChanged: {
        if (active && currentSpeed >= peakSpeed)
            peakSpeedText = currentSpeedText
    }
    Component.onCompleted: {
        if (active)
            resetPeak()
    }

    RowLayout {
        Layout.fillWidth: true

        QQC2.Label {
            Layout.fillWidth: true
            text: translations.i18n("Transfer rate")
            font.weight: Font.DemiBold
        }

        QQC2.Label {
            text: rate.currentSpeedText
            font.weight: Font.DemiBold
        }

        QQC2.Label {
            text: translations.i18n("Peak: %1", rate.peakSpeedText)
            opacity: 0.7
        }
    }

}
