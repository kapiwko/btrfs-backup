// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.btrfsbackup.plasma

Kirigami.FormLayout {
    id: root

    property string cfg_visibleProfiles
    property string cfg_profileOrder
    property alias cfg_historyCount: historyCount.value
    property alias cfg_autoExpandActive: autoExpandActive.checked
    property alias cfg_autoExpandFailed: autoExpandFailed.checked
    property alias cfg_showStorage: showStorage.checked
    property alias cfg_hideSourceNamesInTooltip: hideSourceNames.checked

    readonly property var configuredVisibleProfiles: root.cfg_visibleProfiles !== "*" && root.cfg_visibleProfiles.length > 0
        ? root.cfg_visibleProfiles.split(",")
        : []
    readonly property var configuredOrder: root.cfg_profileOrder.length > 0
        ? root.cfg_profileOrder.split(",")
        : []

    BackupStatusModel {
        id: profileDirectory
        profile: "default"
        Component.onCompleted: start()
    }

    ListModel {
        id: profileSettings
    }

    Connections {
        target: profileDirectory
        function onProfilesChanged() { root.rebuildProfiles() }
    }

    Component.onCompleted: rebuildProfiles()

    ColumnLayout {
        Kirigami.FormData.label: i18n("Profiles:")
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            model: profileSettings

            delegate: RowLayout {
                id: profileRow
                required property int index
                required property string profileId
                required property string profileName
                required property bool profileVisible
                Layout.fillWidth: true

                QQC2.CheckBox {
                    checked: profileRow.profileVisible
                    text: profileRow.profileName || profileRow.profileId
                    Layout.fillWidth: true
                    onToggled: {
                        profileSettings.setProperty(profileRow.index, "profileVisible", checked)
                        root.storeProfiles()
                    }
                }
                QQC2.ToolButton {
                    enabled: profileRow.index > 0
                    icon.name: "go-up-symbolic"
                    text: i18n("Move up")
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: {
                        profileSettings.move(profileRow.index, profileRow.index - 1, 1)
                        root.storeProfiles()
                    }
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                }
                QQC2.ToolButton {
                    enabled: profileRow.index + 1 < profileSettings.count
                    icon.name: "go-down-symbolic"
                    text: i18n("Move down")
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: {
                        profileSettings.move(profileRow.index, profileRow.index + 1, 1)
                        root.storeProfiles()
                    }
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                }
            }
        }

        QQC2.Label {
            visible: profileSettings.count === 0
            text: profileDirectory.managerConnected
                ? i18n("No backup profiles configured")
                : i18n("Backup service unavailable")
            opacity: 0.7
        }
    }

    QQC2.SpinBox {
        id: historyCount
        Kirigami.FormData.label: i18n("Recent backups:")
        from: 1
        to: 10
    }

    QQC2.CheckBox {
        id: autoExpandActive
        Kirigami.FormData.label: i18n("Expansion:")
        text: i18n("Expand an active backup automatically")
    }

    QQC2.CheckBox {
        id: autoExpandFailed
        text: i18n("Expand a failed backup automatically")
    }

    QQC2.CheckBox {
        id: showStorage
        Kirigami.FormData.label: i18n("Details:")
        text: i18n("Show target storage usage")
    }

    QQC2.CheckBox {
        id: hideSourceNames
        Kirigami.FormData.label: i18n("Privacy:")
        text: i18n("Hide source names in the panel tooltip")
    }

    QQC2.Button {
        Kirigami.FormData.label: i18n("Notifications:")
        icon.name: "preferences-desktop-notification"
        text: i18n("Configure notifications...")
        onClicked: profileDirectory.openNotificationSettings()
    }

    function rebuildProfiles() {
        if (profileDirectory.profiles.length === 0)
            return
        const byId = {}
        for (const profile of profileDirectory.profiles)
            byId[profile.profileId] = profile
        const ordered = []
        for (const profileId of root.configuredOrder) {
            if (byId[profileId] !== undefined) {
                ordered.push(byId[profileId])
                delete byId[profileId]
            }
        }
        for (const profile of profileDirectory.profiles) {
            if (byId[profile.profileId] !== undefined)
                ordered.push(profile)
        }

        profileSettings.clear()
        for (const profile of ordered) {
            profileSettings.append({
                profileId: profile.profileId,
                profileName: profile.name,
                profileVisible: root.cfg_visibleProfiles === "*"
                    || root.configuredVisibleProfiles.indexOf(profile.profileId) >= 0
            })
        }
    }

    function storeProfiles() {
        const visible = []
        const order = []
        for (let index = 0; index < profileSettings.count; ++index) {
            const profile = profileSettings.get(index)
            order.push(profile.profileId)
            if (profile.profileVisible)
                visible.push(profile.profileId)
        }
        root.cfg_visibleProfiles = visible.length === profileSettings.count ? "*" : visible.join(",")
        root.cfg_profileOrder = order.join(",")
    }
}
