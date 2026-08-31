// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

Item {
    id: root

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.gridUnit
        spacing: Kirigami.Units.largeSpacing

        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: Kirigami.Units.gridUnit * 34
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.ItemDelegate {
                id: preparedDevice
                Layout.fillWidth: true
                enabled: false

                contentItem: RowLayout {
                    Kirigami.Icon {
                        source: "drive-harddisk-symbolic"
                        implicitWidth: Kirigami.Units.iconSizes.medium
                        implicitHeight: implicitWidth
                    }
                    Kirigami.TitleSubtitle {
                        Layout.fillWidth: true
                        title: translations.i18n("Use a prepared backup device")
                        subtitle: translations.i18n("Select an existing encrypted Btrfs target")
                        selected: false
                    }
                    Kirigami.Icon {
                        source: preparedDevice.mirrored ? "go-previous-symbolic" : "go-next-symbolic"
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: implicitWidth
                    }
                }
            }

            QQC2.ItemDelegate {
                id: newDevice
                Layout.fillWidth: true
                enabled: false

                contentItem: RowLayout {
                    Kirigami.Icon {
                        source: "tools-wizard-symbolic"
                        implicitWidth: Kirigami.Units.iconSizes.medium
                        implicitHeight: implicitWidth
                    }
                    Kirigami.TitleSubtitle {
                        Layout.fillWidth: true
                        title: translations.i18n("Prepare a new backup device")
                        subtitle: translations.i18n("Partition, encrypt and initialize an empty disk")
                        selected: false
                    }
                    Kirigami.Icon {
                        source: newDevice.mirrored ? "go-previous-symbolic" : "go-next-symbolic"
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: implicitWidth
                    }
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: true
                type: Kirigami.MessageType.Information
                text: translations.i18n("Device setup will become available after the system service gains device discovery and provisioning support.")
            }
        }

        Item { Layout.fillHeight: true }
    }
}
