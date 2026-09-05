// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    id: root

    required property var settings
    title: translations.i18n("Backup notifications")
    enabled: root.settings !== null

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.largeSpacing

        SettingsSection {
            title: translations.i18n("Overdue backups")

            QQC2.CheckBox {
                id: remindersEnabled

                text: translations.i18n("Notify me when a backup is overdue")
                checked: root.settings?.enabled ?? true
                onToggled: root.settings.enabled = checked
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true
                enabled: remindersEnabled.checked

                QQC2.SpinBox {
                    Kirigami.FormData.label: translations.i18n("Warning after:")
                    from: 1
                    to: Math.max(from, criticalDays.value - 1)
                    value: root.settings?.warningDays ?? 7
                    textFromValue: value => translations.i18np("%1 day", "%1 days", value)
                    valueFromText: text => parseInt(text)
                    onValueModified: root.settings.warningDays = value
                }

                QQC2.SpinBox {
                    id: criticalDays

                    Kirigami.FormData.label: translations.i18n("Critical after:")
                    from: (root.settings?.warningDays ?? 7) + 1
                    to: 3650
                    value: root.settings?.criticalDays ?? 14
                    textFromValue: value => translations.i18np("%1 day", "%1 days", value)
                    valueFromText: text => parseInt(text)
                    onValueModified: root.settings.criticalDays = value
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: translations.i18n("The background monitor asks you to connect the backup disk.")
                wrapMode: Text.Wrap
                opacity: 0.75
            }
        }

        SettingsSection {
            title: translations.i18n("Backup target space")

            QQC2.CheckBox {
                id: storageEnabled

                text: translations.i18n("Notify me when free space is running low")
                checked: root.settings?.storageEnabled ?? true
                onToggled: root.settings.storageEnabled = checked
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true
                enabled: storageEnabled.checked

                QQC2.SpinBox {
                    id: storageWarning

                    Kirigami.FormData.label: translations.i18n("Warning below:")
                    from: (root.settings?.storageCriticalPercent ?? 5) + 1
                    to: 99
                    value: root.settings?.storageWarningPercent ?? 15
                    textFromValue: value => translations.i18n("%1% free", value)
                    valueFromText: text => parseInt(text)
                    onValueModified: root.settings.storageWarningPercent = value
                }

                QQC2.SpinBox {
                    Kirigami.FormData.label: translations.i18n("Critical below:")
                    from: 1
                    to: Math.max(from, storageWarning.value - 1)
                    value: root.settings?.storageCriticalPercent ?? 5
                    textFromValue: value => translations.i18n("%1% free", value)
                    valueFromText: text => parseInt(text)
                    onValueModified: root.settings.storageCriticalPercent = value
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: translations.i18n("The critical notification also appears when free space falls below the minimum configured for a profile.")
                wrapMode: Text.Wrap
                opacity: 0.75
            }
        }

        Item { Layout.fillHeight: true }
    }

    component SettingsSection: ColumnLayout {
        required property string title
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: Kirigami.Units.gridUnit * 28
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 3
            text: parent.title
        }
        Kirigami.Separator { Layout.fillWidth: true }
    }
}
