// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

Item {
    id: root

    required property var directory
    required property var profileStatusOverrides
    required property var profileSummaryFor

    signal profileRequested(string profileId)
    signal editProfileRequested(string profileId)

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
                directory: root.directory
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

    }
}
