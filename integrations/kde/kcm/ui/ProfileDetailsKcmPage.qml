// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kcmutils as KCMUtils
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.btrfsbackup.plasma

KCMUtils.SimpleKCM {
    id: root

    required property string profileId
    required property var editor
    required property var directory
    required property var historyModel
    property var statusOverride: null
    property bool editImmediately: false
    property bool openSettingsWhenLoaded: editImmediately
    property int sourceToRemove: -1
    property string sourceNameToRemove: ""
    readonly property var profileStatus: statusOverride ?? liveProfileStatus

    title: root.editor?.name || root.profileId || translations.i18n("Profile details")

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    BackupStatusModel {
        id: liveProfileStatus
        profile: root.profileId
        historyLimit: 10
        Component.onCompleted: {
            if (root.statusOverride === null)
                start();
        }
    }

    actions: [
        Kirigami.Action {
            icon.name: "view-history-symbolic"
            text: translations.i18n("History")
            enabled: root.historyModel !== null
            onTriggered: {
                if (typeof kcm !== "undefined")
                    kcm.push("ProfileHistoryKcmPage.qml", {
                        "historyModel": root.historyModel
                    });
            }
        },
        Kirigami.Action {
            icon.name: "document-edit-symbolic"
            text: translations.i18n("Edit profile")
            enabled: root.editor !== null && root.editor.loaded && !root.editor.busy
            onTriggered: root.openProfileSettings()
        },
        Kirigami.Action {
            id: toggleAutomaticBackupsAction
            text: translations.i18nc("@action: automatic backups enabled", "Enabled")
            checkable: true
            checked: root.profileStatus.profileEnabled
            enabled: root.profileStatus.managerConnected && !root.profileStatus.operationPending
            onToggled: root.profileStatus.setProfileEnabled(!root.profileStatus.profileEnabled)
            displayHint: Kirigami.DisplayHint.KeepVisible
            displayComponent: QQC2.Switch {
                action: toggleAutomaticBackupsAction
            }
        }
    ]

    ProfileDetailsPage {
        anchors.fill: parent
        editor: root.editor
        profileStatus: root.profileStatus
        statusTextFor: state => root.statusText(state)
        targetStateTextFor: state => root.targetStateText(state)
        runningStateFor: state => root.isRunning(state)
        onAddSourceRequested: (name, subvolume, localRetention, targetRetention) => {
            root.editor.addSourceConfiguration(name, subvolume, localRetention, targetRetention);
        }
        onEditSourceRequested: (index, name, localRetention, targetRetention) => {
            root.editor.updateSourceConfiguration(index, name, localRetention, targetRetention);
        }
        onRemoveSourceRequested: (index, source) => {
            root.sourceToRemove = index;
            root.sourceNameToRemove = source.name || source.id || "";
            removeSourceDialog.open();
        }
        onDeleteRequested: deleteDialog.open()
    }

    Connections {
        target: root.editor

        function onConflictDetected() {
            conflictDialog.open();
        }
        function onProfileChanged() {
            if (root.openSettingsWhenLoaded && root.editor.loaded && root.editor.profileId === root.profileId) {
                root.openSettingsWhenLoaded = false;
                root.openProfileSettings();
            }
        }
        function onProfileSaved() {
            root.directory.refreshNow();
            root.editor.loadDetails(root.profileId);
        }
        function onProfileDeleted() {
            root.directory.refreshNow();
            if (typeof kcm !== "undefined")
                kcm.pop();
        }
    }

    QQC2.Dialog {
        id: conflictDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: translations.i18n("Profile changed")
        standardButtons: QQC2.Dialog.NoButton

        contentItem: Column {
            spacing: Kirigami.Units.largeSpacing
            QQC2.Label {
                width: Kirigami.Units.gridUnit * 24
                text: translations.i18n("The profile was changed by another process. Reloading discards local changes.")
                wrapMode: Text.Wrap
            }
            Row {
                spacing: Kirigami.Units.smallSpacing
                QQC2.Button {
                    text: translations.i18n("Keep changes")
                    onClicked: conflictDialog.close()
                }
                QQC2.Button {
                    text: translations.i18n("Reload")
                    highlighted: true
                    onClicked: {
                        conflictDialog.close();
                        root.editor.reload();
                    }
                }
            }
        }
    }

    QQC2.Dialog {
        id: removeSourceDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: translations.i18n("Remove source")
        standardButtons: QQC2.Dialog.Yes | QQC2.Dialog.Cancel
        onAccepted: {
            root.editor.removeSourceConfiguration(root.sourceToRemove);
            root.sourceToRemove = -1;
            root.sourceNameToRemove = "";
        }
        QQC2.Label {
            text: translations.i18n("Remove %1 from this profile? Existing local and target snapshots are not deleted.", root.sourceNameToRemove)
            wrapMode: Text.Wrap
        }
    }

    QQC2.Dialog {
        id: deleteDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: translations.i18n("Delete profile")
        standardButtons: QQC2.Dialog.Yes | QQC2.Dialog.Cancel
        onAccepted: root.editor.deleteProfile()
        QQC2.Label {
            text: translations.i18n("Delete this profile and its managed system configuration? Backup data is not removed.")
            wrapMode: Text.Wrap
        }
    }

    Component.onCompleted: {
        if (root.historyModel !== null)
            root.historyModel.profileId = root.profileId;
        root.editor.loadDetails(root.profileId);
    }

    function openProfileSettings() {
        if (typeof kcm !== "undefined")
            kcm.push("ProfileSettingsPage.qml", {"editor": root.editor});
    }

    function statusText(state) {
        switch (state) {
        case "starting":
        case "running":
            return translations.i18n("Backup is in progress");
        case "validating":
            return translations.i18n("Target validation is in progress");
        case "validated":
            return translations.i18n("Validation completed successfully");
        case "succeeded":
            return translations.i18n("Backup completed successfully");
        case "failed":
            return translations.i18n("Backup failed");
        case "cancelled":
            return translations.i18n("Backup cancelled");
        case "skipped":
            return translations.i18n("Backup skipped");
        default:
            return translations.i18n("No active backup");
        }
    }

    function isRunning(state) {
        return state === "starting" || state === "running" || state === "validating";
    }

    function targetStateText(state) {
        switch (state) {
        case "mounted":
            return translations.i18n("Mounted");
        case "unexpected-mount":
            return translations.i18n("Unexpected mount");
        case "unlocked":
            return translations.i18n("Unlocked");
        case "connected":
            return translations.i18n("Connected");
        case "disconnected":
            return translations.i18n("Disconnected");
        default:
            return translations.i18n("Unknown");
        }
    }
}
