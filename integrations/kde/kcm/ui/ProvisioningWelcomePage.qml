// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root

    required property var workflow
    required property var translations

    Layout.fillWidth: true
    Layout.fillHeight: true

    ColumnLayout {
        objectName: "newProfileWelcomeContent"
        anchors.centerIn: parent
        width: Math.min(
            Math.max(0, parent.width - Kirigami.Units.largeSpacing * 2),
            Kirigami.Units.gridUnit * 34
        )
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Icon {
            Layout.alignment: Qt.AlignHCenter
            source: "drive-removable-media"
            implicitWidth: Kirigami.Units.iconSizes.huge
            implicitHeight: implicitWidth
        }
        Kirigami.Heading {
            Layout.alignment: Qt.AlignHCenter
            text: root.translations.i18n("Add backup profile")
            level: 1
        }
        QQC2.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: root.translations.i18n("Choose how the backup device should be configured.")
            opacity: 0.75
        }
        QQC2.ItemDelegate {
            Layout.fillWidth: true
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Icon {
                    source: "document-import"
                    implicitWidth: Kirigami.Units.iconSizes.large
                    implicitHeight: implicitWidth
                }
                Kirigami.TitleSubtitle {
                    Layout.fillWidth: true
                    title: root.translations.i18n("Use a prepared backup device")
                    subtitle: root.translations.i18n("Assign an existing LUKS2 and Btrfs repository. Nothing will be erased.")
                    selected: false
                }
                Kirigami.Icon {
                    source: "go-next-symbolic"
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: implicitWidth
                }
            }
            onClicked: {
                root.workflow.workflowMode = "adopt"
                root.workflow.step = 1
                root.workflow.provisioning.refresh()
            }
        }
        QQC2.ItemDelegate {
            Layout.fillWidth: true
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Icon {
                    source: "tools-wizard"
                    implicitWidth: Kirigami.Units.iconSizes.large
                    implicitHeight: implicitWidth
                }
                Kirigami.TitleSubtitle {
                    Layout.fillWidth: true
                    title: root.translations.i18n("Prepare a new backup device")
                    subtitle: root.translations.i18n("Create an encrypted target. The selected disk or partition will be modified.")
                    selected: false
                }
                Kirigami.Icon {
                    source: "go-next-symbolic"
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: implicitWidth
                }
            }
            onClicked: {
                root.workflow.workflowMode = "prepare"
                root.workflow.step = 1
                root.workflow.provisioning.refresh()
            }
        }
    }
}
