// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

ColumnLayout {
    id: details

    required property var profileStatus
    required property string targetNameHint
    required property bool running
    required property int progress
    required property bool showStorageDetails
    required property string activityLabel
    required property string sourceLabel
    required property string statusLabel
    required property string targetStateLabel
    required property string lastSuccessLabel
    required property string relativeStorageTime
    required property string etaLabel
    required property string durationLabel
    required property string operationLabel
    required property var historySummaryFor
    required property var relativeTimeFor

    spacing: Kirigami.Units.smallSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "plasma_applet_org.btrfsbackup.plasmoid"
    }

    SectionHeader {
        Layout.fillWidth: true
        visible: details.running
        text: translations.i18n("Current transfer")
    }

    CurrentTransfer {
        Layout.fillWidth: true
        active: details.running
        progress: details.progress
        activityLabel: details.activityLabel
        sourceLabel: details.sourceLabel
        currentSpeed: details.profileStatus.run.speedBps
        currentSpeedText: details.profileStatus.run.speedText
        etaLabel: details.etaLabel
        durationLabel: details.durationLabel
    }

    SectionHeader {
        Layout.fillWidth: true
        text: translations.i18n("Backup target")
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        rowSpacing: Kirigami.Units.smallSpacing / 4
        columnSpacing: Kirigami.Units.smallSpacing

        PlasmaComponents3.Label {
            visible: !details.running
            text: translations.i18n("Status:")
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.smallFont
            opacity: 0.6
        }
        PlasmaComponents3.Label {
            visible: !details.running
            Layout.fillWidth: true
            text: details.statusLabel
            elide: Text.ElideRight
            font: Kirigami.Theme.smallFont
        }

        PlasmaComponents3.Label {
            text: translations.i18n("Name:")
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.smallFont
            opacity: 0.6
        }
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: details.profileStatus.target.name
                || details.profileStatus.run.targetName
                || details.targetNameHint
                || translations.i18n("Unknown")
            elide: Text.ElideMiddle
            font: Kirigami.Theme.smallFont
        }

        PlasmaComponents3.Label {
            text: translations.i18n("Device connection:")
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.smallFont
            opacity: 0.6
        }
        RowLayout {
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Icon {
                source: details.profileStatus.target.connected
                    ? "drive-removable-media-symbolic"
                    : "network-disconnect-symbolic"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: implicitWidth
            }
            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: details.profileStatus.target.connected
                    ? translations.i18n("Connected")
                    : translations.i18n("Disconnected")
                elide: Text.ElideRight
                font: Kirigami.Theme.smallFont
            }
        }

        PlasmaComponents3.Label {
            text: translations.i18n("Target state:")
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.smallFont
            opacity: 0.6
        }
        RowLayout {
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Icon {
                source: details.targetStateIcon(details.profileStatus.target.state)
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: implicitWidth
            }
            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: details.targetStateLabel
                elide: Text.ElideRight
                font: Kirigami.Theme.smallFont
            }
        }

        PlasmaComponents3.Label {
            text: translations.i18n("Automatic backups:")
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.smallFont
            opacity: 0.6
        }
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: details.profileStatus.profileEnabled
                ? translations.i18n("On")
                : translations.i18n("Off")
            font: Kirigami.Theme.smallFont
        }

        PlasmaComponents3.Label {
            text: translations.i18n("Last successful backup:")
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.smallFont
            opacity: 0.6
        }
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: details.lastSuccessLabel
            elide: Text.ElideRight
            font: Kirigami.Theme.smallFont

            HoverHandler {
                id: lastSuccessHover
            }

            QQC2.ToolTip.visible: lastSuccessHover.hovered
                && details.profileStatus.run.lastSuccessAt.length > 0
            QQC2.ToolTip.text: details.profileStatus.run.lastSuccessAt
        }

        Kirigami.InlineMessage {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            visible: details.profileStatus.run.lastAttemptState === "failed"
                && details.profileStatus.run.lastAttemptAt.length > 0
            type: Kirigami.MessageType.Error
            text: translations.i18n("The last backup attempt failed.")
        }

        TargetStorageUsage {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: details.showStorageDetails
            supported: details.profileStatus.target.storageSupported
            known: details.profileStatus.target.storageKnown
            capacityText: details.profileStatus.target.capacityText
            usedText: details.profileStatus.target.usedText
            availableText: details.profileStatus.target.availableText
            usagePercent: details.profileStatus.target.usagePercent
            live: details.profileStatus.target.storageLive
            measuredAt: details.profileStatus.target.storageMeasuredAt
            relativeMeasurementTime: details.relativeStorageTime
            belowMinimum: details.profileStatus.target.spaceBelowMinimum
        }

    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: details.profileStatus.lastError.length > 0
            || details.profileStatus.run.errorCode.length > 0
        type: Kirigami.MessageType.Error
        text: details.profileStatus.lastError || details.statusLabel
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: details.profileStatus.lastOperation.length > 0
            && details.profileStatus.lastError.length === 0
        type: Kirigami.MessageType.Positive
        text: details.operationLabel
    }

    ProfileHistory {
        Layout.fillWidth: true
        entries: details.profileStatus.history.entries
        summaryForEntry: details.historySummaryFor
        relativeTimeFor: details.relativeTimeFor
    }

    function targetStateIcon(state) {
        switch (state) {
        case "mounted": return "drive-harddisk-root-symbolic"
        case "unexpected-mount": return "dialog-warning-symbolic"
        case "unlocked": return "object-unlocked-symbolic"
        case "connected": return "object-locked-symbolic"
        case "disconnected": return "network-disconnect-symbolic"
        default: return "dialog-question-symbolic"
        }
    }
}
