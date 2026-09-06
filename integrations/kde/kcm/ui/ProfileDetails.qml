// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.btrfsbackup.kde as BtrfsBackup

ColumnLayout {
    id: root

    required property var profileStatus
    required property string targetNameHint
    readonly property bool running: BtrfsBackup.ProfilePresentation.isRunning(profileStatus.run.state)

    spacing: Kirigami.Units.smallSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    component SectionHeading: RowLayout {
        required property string text
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing
        QQC2.Label {
            text: parent.text
            font.bold: true
        }
        Kirigami.Separator { Layout.fillWidth: true }
    }

    SectionHeading {
        text: translations.i18n("Current transfer")
        visible: root.running
    }

    GridLayout {
        Layout.fillWidth: true
        visible: root.running
        columns: 2
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.smallSpacing / 2

        QQC2.Label { text: translations.i18n("Activity:"); opacity: 0.65 }
        QQC2.Label {
            Layout.fillWidth: true
            text: BtrfsBackup.ProfilePresentation.activityText(
                translations, root.profileStatus.run.activity, root.profileStatus.run.phase)
        }
        QQC2.Label { text: translations.i18n("Source:"); opacity: 0.65 }
        QQC2.Label {
            Layout.fillWidth: true
            text: root.profileStatus.run.sourceName || translations.i18n("Unknown")
        }
        QQC2.ProgressBar {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            from: 0
            to: 100
            indeterminate: root.profileStatus.run.overallProgress < 0
            value: Math.max(0, root.profileStatus.run.overallProgress)
        }
        QQC2.Label { text: translations.i18n("Progress:"); opacity: 0.65 }
        QQC2.Label {
            text: root.profileStatus.run.overallProgress >= 0
                ? translations.i18n("%1%", root.profileStatus.run.overallProgress)
                : translations.i18n("Calculating")
        }
        QQC2.Label {
            visible: root.profileStatus.run.bytesTotalEstimated > 0
            text: translations.i18n("Transferred:")
            opacity: 0.65
        }
        QQC2.Label {
            visible: root.profileStatus.run.bytesTotalEstimated > 0
            text: translations.i18n(
                "%1 of %2",
                root.profileStatus.run.bytesProcessedText,
                root.profileStatus.run.bytesTotalEstimatedText
            )
        }
        QQC2.Label { text: translations.i18n("Speed:"); opacity: 0.65 }
        QQC2.Label { text: root.profileStatus.run.speedText || translations.i18n("Unknown") }
        QQC2.Label { text: translations.i18n("Elapsed:"); opacity: 0.65 }
        QQC2.Label { text: BtrfsBackup.ProfilePresentation.formatDuration(translations, root.profileStatus.run.elapsedSeconds) }
        QQC2.Label { text: translations.i18n("Remaining:"); opacity: 0.65 }
        QQC2.Label { text: BtrfsBackup.ProfilePresentation.formatEta(translations, root.profileStatus.run.etaSeconds) }
    }

    SectionHeading { text: translations.i18n("Backup target") }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.smallSpacing / 2

        QQC2.Label { text: translations.i18n("Status:"); opacity: 0.65 }
        QQC2.Label {
            Layout.fillWidth: true
            text: BtrfsBackup.ProfilePresentation.statusText(translations, root.profileStatus.run.state)
        }
        QQC2.Label { text: translations.i18n("Name:"); opacity: 0.65 }
        QQC2.Label {
            Layout.fillWidth: true
            text: root.profileStatus.target.name
                || root.profileStatus.run.targetName
                || root.targetNameHint
                || translations.i18n("Unknown")
            elide: Text.ElideMiddle
        }
        QQC2.Label { text: translations.i18n("Device connection:"); opacity: 0.65 }
        RowLayout {
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Icon {
                source: BtrfsBackup.ProfilePresentation.deviceConnectionIcon(root.profileStatus.target.connected)
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: implicitWidth
            }
            QQC2.Label {
                objectName: "deviceConnectionState"
                text: root.profileStatus.target.connected
                    ? translations.i18n("Connected")
                    : translations.i18n("Disconnected")
            }
        }
        QQC2.Label { text: translations.i18n("Target state:"); opacity: 0.65 }
        RowLayout {
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Icon {
                source: BtrfsBackup.ProfilePresentation.targetStateIcon(root.profileStatus.target.state)
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: implicitWidth
            }
            QQC2.Label {
                text: BtrfsBackup.ProfilePresentation.targetStateText(
                    translations, root.profileStatus.target.state, root.profileStatus.target.safeToRemove)
            }
        }
        QQC2.Label { text: translations.i18n("Automatic backups:"); opacity: 0.65 }
        QQC2.Label {
            text: root.profileStatus.profileEnabled
                ? translations.i18n("Enabled")
                : translations.i18n("Disabled")
        }
        QQC2.Label { text: translations.i18n("Last successful backup:"); opacity: 0.65 }
        QQC2.Label {
            Layout.fillWidth: true
            text: root.lastSuccessDateTime(root.profileStatus.run.lastSuccessAt)
        }
        QQC2.Label {
            visible: root.profileStatus.target.storageKnown
            text: translations.i18n("Storage usage:")
            opacity: 0.65
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.profileStatus.target.storageKnown
            spacing: Kirigami.Units.smallSpacing / 2
            QQC2.ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: Math.max(0, root.profileStatus.target.usagePercent)
            }
            QQC2.Label {
                text: translations.i18n("%1 used, %2 available",
                    root.profileStatus.target.usedText,
                    root.profileStatus.target.availableText)
                font: Kirigami.Theme.smallFont
                opacity: 0.75
            }
        }
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: root.profileStatus.lastError.length > 0
            || root.profileStatus.run.errorCode.length > 0
        type: Kirigami.MessageType.Error
        text: root.profileStatus.lastError
            || (root.profileStatus.run.errorCode === "backup.failed"
                ? translations.i18n("Backup failed")
                : translations.i18n("Backup failed with code %1", root.profileStatus.run.errorCode))
    }

    function dateTime(value, fallback) {
        const parsed = Date.parse(value)
        return isNaN(parsed) ? fallback : Qt.formatDateTime(new Date(parsed), Locale.ShortFormat)
    }

    function lastSuccessDateTime(value) {
        const timestamp = Date.parse(value)
        if (isNaN(timestamp))
            return translations.i18n("No successful backup")
        const completed = new Date(timestamp)
        const today = new Date()
        const completedDay = new Date(completed.getFullYear(), completed.getMonth(), completed.getDate())
        const todayDay = new Date(today.getFullYear(), today.getMonth(), today.getDate())
        const days = Math.round((todayDay.getTime() - completedDay.getTime()) / 86400000)
        const time = Qt.formatTime(completed, Locale.ShortFormat)
        if (days === 0)
            return translations.i18n("Today at %1", time)
        if (days === 1)
            return translations.i18n("Yesterday at %1", time)
        if (days > 1 && days < 7)
            return translations.i18np("1 day ago at %2", "%1 days ago at %2", days, time)
        return Qt.formatDateTime(completed, "d MMMM yyyy, HH:mm")
    }

}
