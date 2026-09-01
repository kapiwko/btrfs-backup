// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kcmutils as KCMUtils
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.btrfsbackup.plasma

KCMUtils.ScrollViewKCM {
    id: root

    title: translations.i18n("Backup profiles")
    framedView: false
    implicitWidth: Kirigami.Units.gridUnit * 34
    implicitHeight: Kirigami.Units.gridUnit * 30

    property var editorOverride: null
    property var directoryOverride: null
    property var historyOverride: null
    property var profileStatusOverrides: ({})
    readonly property var editor: editorOverride !== null
        ? editorOverride
        : (typeof kcm !== "undefined" ? kcm.profileConfiguration : null)
    readonly property var directory: directoryOverride !== null ? directoryOverride : liveDirectory
    readonly property var historyModel: historyOverride !== null
        ? historyOverride
        : (typeof kcm !== "undefined" ? kcm.profileHistory : null)
    readonly property var credentialModel: typeof kcm !== "undefined" ? kcm.targetCredentials : null
    readonly property var provisioningModel: typeof kcm !== "undefined" ? kcm.deviceProvisioning : null

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

    actions: Kirigami.Action {
        icon.name: "list-add-symbolic"
        text: translations.i18n("Add profile")
        onTriggered: root.openNewProfilePage()
    }

    view: Flickable {
        contentWidth: width
        contentHeight: height
        interactive: false
        clip: true

        ProfilesPage {
            anchors.fill: parent
            directory: root.directory
            profileStatusOverrides: root.profileStatusOverrides
            profileSummaryFor: (status, profile) => root.profileSummary(status, profile)
            runningStateFor: state => root.isRunning(state)
            onProfileRequested: profileId => root.openProfileDetails(profileId, false)
            onEditProfileRequested: profileId => root.openProfileDetails(profileId, true)
            onSystemLogRequested: if (typeof kcm !== "undefined") kcm.openSystemLog()
            onSupportRequested: if (typeof kcm !== "undefined") kcm.openSupportPage()
        }

        Loader {
            id: previewPage
            anchors.fill: parent
            visible: status === Loader.Ready
        }
    }

    function pageProperties(profileId, editImmediately) {
        return {
            "profileId": profileId,
            "editor": root.editor,
            "directory": root.directory,
            "historyModel": root.historyModel,
            "credentialModel": root.credentialModel,
            "statusOverride": root.profileStatusOverrides[profileId] ?? null,
            "editImmediately": editImmediately
        }
    }

    function openEditorFor(profileId) {
        root.openProfileDetails(profileId, false)
    }

    function openProfileDetails(profileId, editImmediately) {
        if (root.editor === null || profileId.length === 0)
            return
        const properties = root.pageProperties(profileId, editImmediately)
        if (typeof kcm !== "undefined")
            kcm.push("ProfileDetailsKcmPage.qml", properties)
        else
            previewPage.setSource("ProfileDetailsKcmPage.qml", properties)
    }

    function openNewProfilePage() {
        if (typeof kcm !== "undefined")
            kcm.push("NewProfilePage.qml", {"editor": root.editor, "provisioning": root.provisioningModel})
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
        if (!status.configurationValid)
            return root.configurationErrorText(status.configurationErrorCode)
        const target = status.target.name || profile.targetName || translations.i18n("Unknown")
        return root.statusText(status.run.state)
            + " - " + target
            + " - " + root.targetStateText(status.target.state)
    }

    function configurationErrorText(code) {
        switch (code) {
        case "configuration.source_missing":
            return translations.i18n("A configured source subvolume does not exist.")
        case "configuration.source_not_subvolume":
            return translations.i18n("A configured source path is not a Btrfs subvolume.")
        default:
            return translations.i18n("A configured source subvolume cannot be inspected.")
        }
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
