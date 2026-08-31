// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.btrfsbackup.plasma

KCMUtils.ScrollViewKCM {
    id: root

    title: root.headerTitle()
    framedView: false
    implicitWidth: Kirigami.Units.gridUnit * 34
    implicitHeight: Kirigami.Units.gridUnit * 30

    property var editorOverride: null
    property var directoryOverride: null
    property var historyOverride: null
    property var profileStatusOverrides: ({})
    property string currentPage: "profiles"
    property string selectedProfileId: ""
    property string pendingAuthorizedAction: ""
    property int pendingSourceIndex: -1
    readonly property var editor: editorOverride !== null
        ? editorOverride
        : (typeof kcm !== "undefined" ? kcm.profileConfiguration : null)
    readonly property var directory: directoryOverride !== null ? directoryOverride : liveDirectory
    readonly property var selectedStatus: profileStatusOverrides[selectedProfileId] ?? liveProfileStatus
    readonly property var historyModel: historyOverride !== null
        ? historyOverride
        : (typeof kcm !== "undefined" ? kcm.profileHistory : null)

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    BackupStatusModel {
        id: liveDirectory
        profile: "default"
        Component.onCompleted: {
            if (root.directoryOverride === null)
                start()
        }
    }

    BackupStatusModel {
        id: liveProfileStatus
        profile: root.selectedProfileId.length > 0 ? root.selectedProfileId : "default"
        historyLimit: 10
        Component.onCompleted: start()
    }

    actions: [
        Kirigami.Action {
            icon.name: "go-previous-symbolic"
            text: translations.i18n("Back")
            visible: root.currentPage !== "profiles"
            onTriggered: root.showProfiles()
        },
        Kirigami.Action {
            icon.name: "list-add-symbolic"
            text: translations.i18n("Add profile")
            visible: root.currentPage === "profiles"
            onTriggered: root.currentPage = "new-profile"
        },
        Kirigami.Action {
            icon.name: "document-edit-symbolic"
            text: translations.i18n("Edit profile")
            visible: root.currentPage === "profile-details"
            enabled: root.editor !== null && root.editor.loaded && !root.editor.busy
            onTriggered: root.authorizeProfileAction("profile-settings")
        },
        Kirigami.Action {
            id: toggleAutomaticBackupsAction
            text: translations.i18nc("@action: automatic backups enabled", "Enabled")
            visible: root.currentPage === "profile-details"
            checkable: true
            checked: root.selectedStatus.profileEnabled
            enabled: root.selectedStatus.managerConnected
                && !root.selectedStatus.operationPending
            onToggled: root.selectedStatus.setProfileEnabled(
                !root.selectedStatus.profileEnabled)
            displayHint: Kirigami.DisplayHint.KeepVisible
            displayComponent: QQC2.Switch {
                action: toggleAutomaticBackupsAction
            }
        }
    ]

    view: Flickable {
        contentWidth: width
        contentHeight: height
        interactive: false
        clip: true

        StackLayout {
            anchors.fill: parent
            currentIndex: root.currentPage === "profiles" ? 0
                : root.currentPage === "profile-details" ? 1 : 2

            ProfilesPage {
                directory: root.directory
                profileStatusOverrides: root.profileStatusOverrides
                profileSummaryFor: (status, profile) => root.profileSummary(status, profile)
                runningStateFor: state => root.isRunning(state)
                onProfileRequested: profileId => root.openProfileDetails(profileId)
                onEditProfileRequested: profileId => root.editProfile(profileId)
                onSystemLogRequested: if (typeof kcm !== "undefined") kcm.openSystemLog()
                onSupportRequested: if (typeof kcm !== "undefined") kcm.openSupportPage()
            }

            ProfileDetailsPage {
                editor: root.editor
                profileStatus: root.selectedStatus
                historyModel: root.historyModel
                statusTextFor: state => root.statusText(state)
                targetStateTextFor: state => root.targetStateText(state)
                runningStateFor: state => root.isRunning(state)
                onAddSourceRequested: root.authorizeProfileAction("source-add")
                onEditSourceRequested: (index, source) => {
                    root.pendingSourceIndex = index
                    root.authorizeProfileAction("source-edit")
                }
                onRemoveSourceRequested: (index, source) => {
                    root.pendingSourceIndex = index
                    root.authorizeProfileAction("source-remove")
                }
                onDeleteRequested: deleteDialog.open()
            }

            NewProfilePage {}
        }
    }

    Connections {
        target: root.editor

        function onConflictDetected() { conflictDialog.open() }
        function onStateChanged() {
            if (root.pendingAuthorizedAction.length === 0 || root.editor === null
                    || root.editor.busy || !root.editor.loaded
                    || root.editor.errorCode.length > 0
                    || root.editor.profileId !== root.selectedProfileId)
                return
            const action = root.pendingAuthorizedAction
            root.pendingAuthorizedAction = ""
            if (action === "profile-settings") {
                profileSettingsDialog.openForProfile()
            } else if (action === "source-add") {
                sourceDialog.openForAdd()
            } else if (action === "source-edit") {
                sourceDialog.openForEdit(root.pendingSourceIndex,
                    root.editor.sources[root.pendingSourceIndex])
            } else if (action === "source-remove") {
                root.sourceToRemove = root.pendingSourceIndex
                const source = root.editor.sources[root.pendingSourceIndex]
                root.sourceNameToRemove = source.name || source.id || ""
                removeSourceDialog.open()
            }
            root.pendingSourceIndex = -1
        }
        function onProfileSaved(profileId) {
            root.directory.refreshNow()
            root.openProfileDetails(profileId)
        }
        function onProfileDeleted(profileId) {
            root.showProfiles()
            root.directory.refreshNow()
        }
    }

    QQC2.Dialog {
        id: conflictDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: translations.i18n("Profile changed")
        standardButtons: QQC2.Dialog.NoButton

        contentItem: ColumnLayout {
            QQC2.Label {
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                text: translations.i18n("The profile was changed by another process. Reloading discards local changes.")
                wrapMode: Text.Wrap
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                QQC2.Button {
                    text: translations.i18n("Keep changes")
                    onClicked: conflictDialog.close()
                }
                QQC2.Button {
                    text: translations.i18n("Reload")
                    highlighted: true
                    onClicked: {
                        conflictDialog.close()
                        root.editor.reload()
                    }
                }
            }
        }
    }

    SourceDialog {
        id: sourceDialog
        editor: root.editor
    }

    ProfileSettingsDialog {
        id: profileSettingsDialog
        editor: root.editor
    }

    property int sourceToRemove: -1
    property string sourceNameToRemove: ""

    QQC2.Dialog {
        id: removeSourceDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: translations.i18n("Remove source")
        standardButtons: QQC2.Dialog.Yes | QQC2.Dialog.Cancel
        onAccepted: {
            root.editor.removeSourceConfiguration(root.sourceToRemove)
            root.sourceToRemove = -1
            root.sourceNameToRemove = ""
        }

        QQC2.Label {
            text: translations.i18n(
                "Remove %1 from this profile? Existing local and target snapshots are not deleted.",
                root.sourceNameToRemove)
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

    function openEditorFor(profileId) {
        openProfileDetails(profileId)
    }

    function openProfileDetails(profileId) {
        if (root.editor === null || profileId.length === 0)
            return
        root.selectedProfileId = profileId
        root.editor.loadDetails(profileId)
        if (root.historyModel !== null)
            root.historyModel.profileId = profileId
        root.currentPage = "profile-details"
    }

    function editProfile(profileId) {
        if (root.editor === null || profileId.length === 0)
            return
        root.selectedProfileId = profileId
        if (root.historyModel !== null)
            root.historyModel.profileId = profileId
        root.currentPage = "profile-details"
        root.authorizeProfileAction("profile-settings")
    }

    function authorizeProfileAction(action) {
        if (root.editor === null || root.selectedProfileId.length === 0
                || root.editor.busy)
            return
        root.pendingAuthorizedAction = action
        root.editor.loadForEditing(root.selectedProfileId)
    }

    function showProfiles() {
        root.currentPage = "profiles"
        root.selectedProfileId = ""
        root.pendingAuthorizedAction = ""
        root.pendingSourceIndex = -1
        if (root.historyModel !== null)
            root.historyModel.profileId = ""
    }

    function headerTitle() {
        if (root.currentPage === "new-profile")
            return translations.i18n("Add backup profile")
        if (root.currentPage === "profile-details")
            return root.editor?.name || root.selectedProfileId || translations.i18n("Profile details")
        return translations.i18n("Backup profiles")
    }

    function statusText(state) {
        switch (state) {
        case "starting":
        case "running": return translations.i18n("Backup is in progress")
        case "validating": return translations.i18n("Target validation is in progress")
        case "validated": return translations.i18n("Validation completed successfully")
        case "succeeded": return translations.i18n("Backup completed successfully")
        case "failed": return translations.i18n("Backup failed")
        case "cancelled": return translations.i18n("Backup cancelled")
        case "skipped": return translations.i18n("Backup skipped")
        default: return translations.i18n("No active backup")
        }
    }

    function isRunning(state) {
        return state === "starting" || state === "running" || state === "validating"
    }

    function profileSummary(status, profile) {
        if (status.lastError.length > 0)
            return root.errorText(status)
        const target = status.target.name || profile.targetName || translations.i18n("Unknown")
        return root.statusText(status.run.state)
            + " - " + target
            + " - " + root.targetStateText(status.target.state)
    }

    function targetStateText(state) {
        switch (state) {
        case "mounted": return translations.i18n("Mounted")
        case "unexpected-mount": return translations.i18n("Unexpected mount")
        case "unlocked": return translations.i18n("Unlocked")
        case "connected": return translations.i18n("Connected")
        case "disconnected": return translations.i18n("Disconnected")
        default: return translations.i18n("Unknown")
        }
    }

    function errorText(status) {
        return status.lastErrorCode.length > 0
            ? translations.i18nc("error message followed by a stable diagnostic code",
                "%1 (code: %2)", status.lastError, status.lastErrorCode)
            : status.lastError
    }
}
