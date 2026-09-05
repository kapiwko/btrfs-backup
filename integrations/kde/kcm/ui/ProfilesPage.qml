// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

Item {
    id: root

    required property var directory
    required property var profileStatusOverrides
    required property var profileSummaryFor
    required property var reminderSettings

    signal profileRequested(string profileId)
    signal editProfileRequested(string profileId)
    signal systemLogRequested()
    signal supportRequested()

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            visible: !root.directory.managerConnected || root.directory.lastError.length > 0
            type: Kirigami.MessageType.Error
            text: root.directory.lastError.length > 0
                ? root.directory.lastError
                : translations.i18n("Backup service unavailable")
        }

        ListView {
            id: profilesView

            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.directory.profiles
            clip: true
            focus: true
            activeFocusOnTab: true
            boundsBehavior: Flickable.StopAtBounds
            currentIndex: -1

            delegate: ProfileDelegate {
                statusOverride: root.profileStatusOverrides[modelData.profileId] ?? null
                profileSummaryFor: root.profileSummaryFor
                onDetailsRequested: profileId => root.profileRequested(profileId)
                onEditRequested: profileId => root.editProfileRequested(profileId)
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: Math.min(parent.width - Kirigami.Units.gridUnit * 4, implicitWidth)
                visible: profilesView.count === 0
                icon.name: root.directory.managerConnected
                    ? "drive-harddisk-symbolic"
                    : "network-disconnect-symbolic"
                text: root.directory.managerConnected
                    ? translations.i18n("No backup profiles configured")
                    : translations.i18n("Waiting for the system backup service")
            }
        }

        Kirigami.Separator { Layout.fillWidth: true }

        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: Kirigami.Units.gridUnit * 30
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 3
                text: translations.i18n("Backup reminders")
            }

            QQC2.CheckBox {
                id: remindersEnabled

                Layout.fillWidth: true
                text: translations.i18n("Notify me when a backup is overdue")
                checked: root.reminderSettings?.enabled ?? true
                enabled: root.reminderSettings !== null
                onToggled: if (root.reminderSettings !== null)
                    root.reminderSettings.enabled = checked
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true
                enabled: remindersEnabled.checked && root.reminderSettings !== null

                QQC2.SpinBox {
                    Kirigami.FormData.label: translations.i18n("Warning after:")
                    from: 1
                    to: Math.max(from, criticalDays.value - 1)
                    value: root.reminderSettings?.warningDays ?? 7
                    textFromValue: value => translations.i18np("%1 day", "%1 days", value)
                    valueFromText: text => parseInt(text)
                    onValueModified: root.reminderSettings.warningDays = value
                }

                QQC2.SpinBox {
                    id: criticalDays

                    Kirigami.FormData.label: translations.i18n("Critical after:")
                    from: (root.reminderSettings?.warningDays ?? 7) + 1
                    to: 3650
                    value: root.reminderSettings?.criticalDays ?? 14
                    textFromValue: value => translations.i18np("%1 day", "%1 days", value)
                    valueFromText: text => parseInt(text)
                    onValueModified: root.reminderSettings.criticalDays = value
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: translations.i18n("The background monitor sends a desktop notification asking you to connect the backup disk.")
                wrapMode: Text.Wrap
                opacity: 0.75
            }
        }

        Kirigami.Separator { Layout.fillWidth: true }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.largeSpacing
            text: translations.i18n("Administrative operations are recorded in the system journal.")
            wrapMode: Text.Wrap
            opacity: 0.75
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                icon.name: "preferences-desktop-notification-symbolic"
                text: translations.i18n("Notifications")
                onClicked: root.directory.openNotificationSettings()
            }
            QQC2.Button {
                icon.name: "view-list-text"
                text: translations.i18n("Open system log")
                onClicked: root.systemLogRequested()
            }
            QQC2.Button {
                icon.name: "help-contents"
                text: translations.i18n("Support")
                onClicked: root.supportRequested()
            }
            Item { Layout.fillWidth: true }
        }
    }
}
