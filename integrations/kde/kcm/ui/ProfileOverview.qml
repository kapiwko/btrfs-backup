// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    required property var editor
    required property var profileStatus
    required property var statusTextFor
    required property var targetStateTextFor
    required property var runningStateFor

    spacing: Kirigami.Units.largeSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: root.editor !== null && root.editor.errorMessage.length > 0
        type: Kirigami.MessageType.Error
        text: root.editor !== null
            ? translations.i18nc("error message followed by a stable diagnostic code",
                "%1 (code: %2)", root.editor.errorMessage, root.editor.errorCode)
            : ""
    }

    ProfileDetails {
        Layout.fillWidth: true
        profileStatus: root.profileStatus
        targetNameHint: root.editor?.target?.device ?? ""
        statusTextFor: root.statusTextFor
        targetStateTextFor: root.targetStateTextFor
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Button {
            icon.name: root.runningStateFor(root.profileStatus.run.state)
                ? "media-playback-stop-symbolic"
                : "media-playback-start-symbolic"
            text: root.runningStateFor(root.profileStatus.run.state)
                ? translations.i18n("Cancel backup")
                : translations.i18n("Start backup")
            enabled: root.profileStatus.managerConnected
                && !root.profileStatus.operationPending
                && (root.runningStateFor(root.profileStatus.run.state)
                    ? root.profileStatus.run.canCancel
                    : root.profileStatus.target.connected)
            onClicked: {
                if (root.runningStateFor(root.profileStatus.run.state))
                    root.profileStatus.cancelBackup()
                else
                    root.profileStatus.startBackup()
            }
        }
        QQC2.Button {
            icon.name: "folder-open-symbolic"
            text: translations.i18n("Browse backups")
            visible: root.profileStatus.browseSupported && root.profileStatus.target.connected
            enabled: root.profileStatus.managerConnected && !root.profileStatus.operationPending
            onClicked: root.profileStatus.browseBackups()
        }
        QQC2.Button {
            icon.name: "media-eject-symbolic"
            text: translations.i18n("Eject")
            visible: root.profileStatus.target.connected
            enabled: root.profileStatus.managerConnected
                && !root.profileStatus.operationPending
                && !root.runningStateFor(root.profileStatus.run.state)
            onClicked: root.profileStatus.ejectTarget()
        }
        Item { Layout.fillWidth: true }
    }
}
