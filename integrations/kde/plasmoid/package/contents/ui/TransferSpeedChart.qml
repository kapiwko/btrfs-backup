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

    required property bool active
    required property real currentSpeed
    property var samples: []
    property real peakSpeed: 0
    readonly property int sampleLimit: 60

    spacing: Kirigami.Units.smallSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "plasma_applet_org.btrfsbackup.plasmoid"
    }

    function formatBytes(value) {
        var amount = Number(value || 0)
        var units = ["B", "KiB", "MiB", "GiB", "TiB", "PiB"]
        var index = 0
        while (amount >= 1024 && index < units.length - 1) {
            amount /= 1024
            index++
        }
        return (index === 0 ? amount.toFixed(0) : amount.toFixed(1)) + " " + units[index]
    }

    function rateText(value) {
        return translations.i18n("%1/s", formatBytes(value))
    }

    function resetSamples() {
        samples = []
        peakSpeed = 0
        appendSample()
    }

    function appendSample() {
        if (!active)
            return
        var value = Math.max(0, Number(currentSpeed || 0))
        var next = samples.slice(Math.max(0, samples.length - sampleLimit + 1))
        next.push(value)
        samples = next
        peakSpeed = Math.max(peakSpeed, value)
    }

    onActiveChanged: {
        if (active)
            resetSamples()
    }
    Component.onCompleted: {
        if (active)
            resetSamples()
    }

    Timer {
        interval: 1000
        running: root.active
        repeat: true
        onTriggered: root.appendSample()
    }

    RowLayout {
        Layout.fillWidth: true

        QQC2.Label {
            text: translations.i18n("Transfer rate")
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }

        QQC2.Label {
            text: root.rateText(root.currentSpeed)
            font.weight: Font.DemiBold
        }

        QQC2.Label {
            text: translations.i18n("Peak: %1", root.rateText(root.peakSpeed))
            opacity: 0.7
        }
    }

    Canvas {
        id: chart
        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 5
        Layout.minimumHeight: Kirigami.Units.gridUnit * 4

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        Connections {
            target: root
            function onSamplesChanged() { chart.requestPaint() }
        }

        onPaint: {
            var context = getContext("2d")
            context.clearRect(0, 0, width, height)
            if (width <= 0 || height <= 0)
                return

            context.save()
            context.strokeStyle = Kirigami.Theme.textColor
            context.lineWidth = 1
            context.globalAlpha = 0.12
            for (var line = 1; line < 4; ++line) {
                var y = Math.round(height * line / 4) + 0.5
                context.beginPath()
                context.moveTo(0, y)
                context.lineTo(width, y)
                context.stroke()
            }
            context.restore()

            if (root.samples.length === 0)
                return

            var maximum = Math.max(1, root.peakSpeed)
            var step = width / Math.max(1, root.sampleLimit - 1)
            var firstX = width - (root.samples.length - 1) * step

            context.save()
            context.beginPath()
            context.moveTo(firstX, height)
            for (var index = 0; index < root.samples.length; ++index) {
                var x = firstX + index * step
                var sampleY = height - (root.samples[index] / maximum) * (height - 2)
                context.lineTo(x, sampleY)
            }
            context.lineTo(width, height)
            context.closePath()
            context.fillStyle = Kirigami.Theme.highlightColor
            context.globalAlpha = 0.18
            context.fill()
            context.restore()

            context.save()
            context.beginPath()
            for (index = 0; index < root.samples.length; ++index) {
                x = firstX + index * step
                sampleY = height - (root.samples[index] / maximum) * (height - 2)
                if (index === 0)
                    context.moveTo(x, sampleY)
                else
                    context.lineTo(x, sampleY)
            }
            context.strokeStyle = Kirigami.Theme.highlightColor
            context.lineWidth = 2
            context.lineJoin = "round"
            context.stroke()
            context.restore()
        }
    }
}
