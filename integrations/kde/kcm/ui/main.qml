// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kcmutils as KCMUtils
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.btrfsbackup.kde
import org.btrfsbackup.kde as BtrfsBackup

KCMUtils.ScrollViewKCM {
    id: root

    title: translations.i18n("Backup profiles")
    framedView: false
    implicitWidth: Kirigami.Units.gridUnit * 34
    implicitHeight: Kirigami.Units.gridUnit * 30

    property var editorOverride: null
    property var directoryOverride: null
    property var historyOverride: null
    property var reminderSettingsOverride: null
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
    readonly property var reminderSettings: reminderSettingsOverride !== null
        ? reminderSettingsOverride
        : (typeof kcm !== "undefined" ? kcm.backupReminderSettings : null)

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    ProfileDirectoryModel {
        id: liveDirectory
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
            onReminderSettingsRequested: root.openReminderSettings()
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

    function openReminderSettings() {
        const properties = {"settings": root.reminderSettings}
        if (typeof kcm !== "undefined")
            kcm.push("NotificationSettingsPage.qml", properties)
        else
            previewPage.setSource("NotificationSettingsPage.qml", properties)
    }

    function profileSummary(status, profile) {
        if (status.lastError.length > 0)
            return root.errorText(status)
        if (!status.configurationValid)
            return BtrfsBackup.ProfilePresentation.configurationErrorText(translations, status.configurationErrorCode)
        const target = status.target.name || profile.targetName || translations.i18n("Unknown")
        return BtrfsBackup.ProfilePresentation.statusText(translations, status.run.state)
            + " - " + target
            + " - " + BtrfsBackup.ProfilePresentation.targetStateText(
                translations, status.target.state, status.target.safeToRemove)
    }

    function errorText(status) {
        return status.lastErrorCode.length > 0
            ? translations.i18nc("error message followed by a stable diagnostic code",
                "%1 (code: %2)", status.lastError, status.lastErrorCode)
            : status.lastError
    }
}
