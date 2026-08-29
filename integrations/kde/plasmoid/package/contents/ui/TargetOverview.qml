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

    required property var profiles
    required property string currentProfile
    required property string targetName
    required property string connectionText
    required property string targetStateText
    required property bool managerConnected
    required property bool targetStateKnown
    required property bool targetConnected
    required property bool operationPending

    signal profileSelected(string profileId)

    spacing: Kirigami.Units.largeSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "plasma_applet_org.btrfsbackup.plasmoid"
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true

        QQC2.ComboBox {
            id: profileSelector
            Kirigami.FormData.label: translations.i18n("Profile:")
            model: root.profiles
            textRole: "name"
            valueRole: "profileId"
            Layout.fillWidth: true
            enabled: !root.operationPending && count > 1
            onActivated: root.profileSelected(currentValue)

            function syncProfile() {
                for (var index = 0; index < count; ++index) {
                    if (valueAt(index) === root.currentProfile) {
                        currentIndex = index
                        return
                    }
                }
            }

            Component.onCompleted: syncProfile()
        }
    }

    onProfilesChanged: profileSelector.syncProfile()
    onCurrentProfileChanged: profileSelector.syncProfile()

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Icon {
            source: root.targetConnected ? "drive-harddisk" : "drive-removable-media"
            implicitWidth: Kirigami.Units.iconSizes.medium
            implicitHeight: implicitWidth
            opacity: root.targetConnected ? 1 : 0.55
        }

        ColumnLayout {
            Layout.fillWidth: true

            QQC2.Label {
                text: root.targetName || translations.i18n("Backup target")
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }

            QQC2.Label {
                text: root.connectionText + " - " + root.targetStateText
                opacity: 0.7
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: root.managerConnected && root.targetStateKnown && !root.targetConnected
        type: Kirigami.MessageType.Warning
        text: translations.i18n("Connect the backup target to run or validate a backup.")
    }
}
