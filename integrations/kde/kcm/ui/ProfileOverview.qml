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

    required property var editor
    required property var profileStatus
    required property var statusTextFor
    required property var targetStateTextFor
    readonly property bool authorizationError: root.editor !== null && root.editor.errorCode.endsWith(".NotAuthorized")

    spacing: Kirigami.Units.largeSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: root.editor !== null && root.editor.errorMessage.length > 0
        type: root.authorizationError ? Kirigami.MessageType.Warning : Kirigami.MessageType.Error
        text: root.errorText()
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: root.editor !== null && root.editor.loaded && !root.editor.configurationValid
        type: Kirigami.MessageType.Error
        text: root.configurationErrorText(root.editor?.configurationErrorCode ?? "")
    }

    Timer {
        id: authorizationErrorTimer

        interval: 7000
        running: root.authorizationError
        onTriggered: {
            if (root.editor !== null && typeof root.editor.clearError === "function")
                root.editor.clearError();
        }
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
            icon.name: BtrfsBackup.ProfilePresentation.isRunning(root.profileStatus.run.state) ? "media-playback-stop-symbolic" : "media-playback-start-symbolic"
            text: BtrfsBackup.ProfilePresentation.isRunning(root.profileStatus.run.state) ? translations.i18n("Cancel backup") : translations.i18n("Start backup")
            enabled: BtrfsBackup.ProfilePresentation.primaryActionEnabled(root.profileStatus)
            onClicked: {
                if (BtrfsBackup.ProfilePresentation.isRunning(root.profileStatus.run.state))
                    root.profileStatus.cancelBackup();
                else
                    root.profileStatus.startBackup();
            }
        }
        QQC2.Button {
            objectName: "browseBackupsButton"
            icon.name: "folder-open-symbolic"
            text: translations.i18n("Browse backups")
            visible: root.profileStatus.browseSupported
            enabled: BtrfsBackup.ProfilePresentation.canBrowse(root.profileStatus)
            onClicked: root.profileStatus.browseBackups()
        }
        QQC2.Button {
            icon.name: "media-eject-symbolic"
            text: translations.i18n("Eject")
            visible: root.profileStatus.target.connected
            enabled: BtrfsBackup.ProfilePresentation.canEject(root.profileStatus)
            onClicked: root.profileStatus.ejectTarget()
        }
        Item {
            Layout.fillWidth: true
        }
    }

    function errorText() {
        if (root.editor === null || root.editor.errorMessage.length === 0)
            return "";
        if (root.authorizationError) {
            return translations.i18n("The operation was cancelled or you do not have permission to perform it.");
        }
        if (root.editor.errorCode.endsWith(".SourceMissing"))
            return translations.i18n("The selected source subvolume does not exist.");
        if (root.editor.errorCode.endsWith(".SourceNotSubvolume"))
            return translations.i18n("The selected source path is not a Btrfs subvolume.");
        if (root.editor.errorCode.endsWith(".SourceUnavailable"))
            return translations.i18n("The selected source subvolume cannot be inspected.");
        return translations.i18nc("error message followed by a stable diagnostic code", "%1 (code: %2)", root.editor.errorMessage, root.editor.errorCode);
    }

    function configurationErrorText(code) {
        switch (code) {
        case "configuration.source_missing":
            return translations.i18n("A configured source subvolume does not exist.");
        case "configuration.source_not_subvolume":
            return translations.i18n("A configured source path is not a Btrfs subvolume.");
        default:
            return translations.i18n("A configured source subvolume cannot be inspected.");
        }
    }
}
