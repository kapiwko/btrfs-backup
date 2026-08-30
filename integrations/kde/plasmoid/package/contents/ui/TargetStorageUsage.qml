// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

ColumnLayout {
    id: root

    required property bool supported
    required property bool known
    required property real capacityBytes
    required property real usedBytes
    required property real availableBytes
    required property int usagePercent
    required property bool live
    required property string measuredAt
    required property string relativeMeasurementTime
    required property bool belowMinimum

    readonly property string capacityText: formatBytes(capacityBytes)
    readonly property string usedText: formatBytes(usedBytes)
    readonly property string availableText: formatBytes(availableBytes)
    readonly property string usageText: usagePercent + "%"

    visible: supported
    spacing: Kirigami.Units.smallSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "plasma_applet_org.btrfsbackup.plasmoid"
    }

    function formatBytes(value) {
        var amount = Math.max(0, Number(value || 0))
        if (!isFinite(amount))
            return "0 B"
        var units = ["B", "KiB", "MiB", "GiB", "TiB", "PiB"]
        var index = 0
        while (amount >= 1024 && index < units.length - 1) {
            amount /= 1024
            index++
        }
        return (index === 0 ? amount.toFixed(0) : amount.toFixed(1)) + " " + units[index]
    }

    PlasmaComponents3.Label {
        Layout.fillWidth: true
        visible: !root.known
        text: translations.i18n("No capacity data")
        wrapMode: Text.Wrap
        font: Kirigami.Theme.smallFont
        opacity: 0.7
    }

    GridLayout {
        Layout.fillWidth: true
        visible: root.known
        columns: 2
        rowSpacing: Kirigami.Units.smallSpacing / 4
        columnSpacing: Kirigami.Units.smallSpacing

        PlasmaComponents3.Label {
            text: translations.i18n("Capacity:")
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.smallFont
            opacity: 0.6
        }
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: root.capacityText
            font: Kirigami.Theme.smallFont
        }

        PlasmaComponents3.Label {
            text: translations.i18n("Used:")
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.smallFont
            opacity: 0.6
        }
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: root.usedText
            font: Kirigami.Theme.smallFont
        }

        PlasmaComponents3.Label {
            text: translations.i18n("Usage:")
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.smallFont
            opacity: 0.6
        }
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: root.usageText
            font: Kirigami.Theme.smallFont
        }

        PlasmaComponents3.Label {
            text: translations.i18n("Available:")
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.smallFont
            opacity: 0.6
        }
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: root.availableText
            font: Kirigami.Theme.smallFont
        }
    }

    QQC2.ProgressBar {
        Layout.fillWidth: true
        visible: root.known
        from: 0
        to: 100
        value: Math.max(0, Math.min(100, root.usagePercent))
        indeterminate: false
    }

    PlasmaComponents3.Label {
        Layout.fillWidth: true
        visible: root.known && !root.live
        text: translations.i18n("Last measurement: %1", root.relativeMeasurementTime)
        elide: Text.ElideRight
        font: Kirigami.Theme.smallFont
        opacity: 0.7

    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.known && root.belowMinimum

        Kirigami.Icon {
            source: "dialog-warning-symbolic"
            implicitWidth: Kirigami.Units.iconSizes.small
            implicitHeight: implicitWidth
        }
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: translations.i18n("Available space is below the configured minimum.")
            color: Kirigami.Theme.neutralTextColor
            wrapMode: Text.Wrap
            font: Kirigami.Theme.smallFont
        }
    }

}
