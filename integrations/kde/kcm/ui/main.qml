// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami
import org.btrfsbackup.plasma

KCMUtils.ScrollViewKCM {
    id: root

    title: i18n("Btrfs Backups")
    implicitWidth: Kirigami.Units.gridUnit * 34
    implicitHeight: Kirigami.Units.gridUnit * 30
    property var editorOverride: null
    readonly property var editor: editorOverride !== null
        ? editorOverride
        : (typeof kcm !== "undefined" ? kcm.profileConfiguration : null)
    BackupStatusModel {
        id: directory
        profile: "default"
        Component.onCompleted: start()
    }

    view: ListView {
        id: profilesView
        model: directory.profiles
        spacing: Kirigami.Units.smallSpacing
        topMargin: Kirigami.Units.largeSpacing
        bottomMargin: Kirigami.Units.largeSpacing
        leftMargin: Kirigami.Units.largeSpacing
        rightMargin: Kirigami.Units.largeSpacing
        clip: true

        header: ColumnLayout {
            width: profilesView.width - profilesView.leftMargin - profilesView.rightMargin
            spacing: Kirigami.Units.smallSpacing

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: !directory.managerConnected || directory.lastError.length > 0
                type: Kirigami.MessageType.Error
                text: directory.lastError.length > 0
                    ? root.errorText(directory)
                    : i18n("Backup service unavailable")
            }
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.editor && root.editor.errorCode.endsWith(".RollbackIncomplete")
                type: Kirigami.MessageType.Warning
                text: i18n("Rollback was incomplete. Review the system log before running another backup.")
            }

            RowLayout {
                Layout.fillWidth: true

                QQC2.Label {
                    Layout.fillWidth: true
                    text: directory.managerConnected
                        ? i18np("1 profile", "%1 profiles", directory.profiles.length)
                        : i18n("Waiting for the system backup service")
                    font.weight: Font.DemiBold
                }
                QQC2.Button {
                    icon.name: "view-refresh"
                    text: i18n("Refresh")
                    enabled: directory.managerConnected
                    onClicked: directory.refreshNow()
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }
        }

        delegate: QQC2.ItemDelegate {
            id: profileRow
            required property var modelData
            width: profilesView.width - profilesView.leftMargin - profilesView.rightMargin
            padding: Kirigami.Units.largeSpacing

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                BackupStatusModel {
                    id: profileStatus
                    profile: profileRow.modelData.profileId
                    historyLimit: 3
                    Component.onCompleted: start()
                }

                RowLayout {
                    Layout.fillWidth: true

                    Kirigami.Icon {
                        source: profileStatus.run.state === "failed"
                            ? "dialog-error-symbolic"
                            : "drive-harddisk-symbolic"
                        implicitWidth: Kirigami.Units.iconSizes.medium
                        implicitHeight: implicitWidth
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        QQC2.Label {
                            text: profileRow.modelData.name || profileRow.modelData.profileId
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                        }
                        QQC2.Label {
                            text: root.statusText(profileStatus.run.state)
                            color: profileStatus.run.state === "failed"
                                ? Kirigami.Theme.negativeTextColor
                                : Kirigami.Theme.textColor
                            opacity: 0.8
                            Layout.fillWidth: true
                        }
                    }
                    QQC2.Button {
                        icon.name: "tools-check-spelling"
                        text: i18n("Validate target")
                        enabled: profileStatus.managerConnected && !profileStatus.operationPending
                        onClicked: profileStatus.validateTarget()
                    }
                    QQC2.Button {
                        icon.name: "document-edit"
                        text: i18n("Configure")
                        enabled: root.editor !== null && !root.editor.busy
                        onClicked: {
                            root.openEditorFor(profileRow.modelData.profileId)
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    QQC2.Label { text: i18n("Last successful backup:"); opacity: 0.65 }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: profileStatus.run.lastSuccessAt || i18n("No successful backup")
                        elide: Text.ElideRight
                    }
                    QQC2.Label { text: i18n("Target:"); opacity: 0.65 }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: profileStatus.target.name || profileRow.modelData.targetName || i18n("Unknown")
                        elide: Text.ElideMiddle
                    }
                    QQC2.Label { text: i18n("Target state:"); opacity: 0.65 }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: root.targetStateText(profileStatus.target.state)
                    }
                    QQC2.Label {
                        visible: profileStatus.target.storageKnown
                        text: i18n("Available space:")
                        opacity: 0.65
                    }
                    QQC2.Label {
                        visible: profileStatus.target.storageKnown
                        Layout.fillWidth: true
                        text: profileStatus.target.availableText
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: profileStatus.lastOperation === "validate-target"
                    type: Kirigami.MessageType.Positive
                    text: i18n("Target validation completed successfully")
                }
                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: profileStatus.lastError.length > 0
                    type: Kirigami.MessageType.Error
                    text: root.errorText(profileStatus)
                }
            }
        }

        footer: ColumnLayout {
            width: profilesView.width - profilesView.leftMargin - profilesView.rightMargin
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Separator { Layout.fillWidth: true }
            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Administrative operation details are recorded in the system journal.")
                wrapMode: Text.Wrap
                opacity: 0.75
            }
            RowLayout {
                Layout.fillWidth: true
                QQC2.Button {
                    icon.name: "view-list-text"
                    text: i18n("Open system log")
                    onClicked: kcm.openSystemLog()
                }
                QQC2.Button {
                    icon.name: "help-contents"
                    text: i18n("Support")
                    onClicked: kcm.openSupportPage()
                }
                Item { Layout.fillWidth: true }
            }
        }
    }

    Connections {
        target: root.editor
        function onConflictDetected() { conflictDialog.open() }
        function onProfileSaved(profileId) { directory.refreshNow() }
        function onProfileDeleted(profileId) {
            editorDialog.close()
            directory.refreshNow()
        }
    }

    QQC2.Dialog {
        id: editorDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Kirigami.Units.gridUnit * 2 : root.width, Kirigami.Units.gridUnit * 46)
        height: Math.min(parent ? parent.height - Kirigami.Units.gridUnit * 2 : root.height, Kirigami.Units.gridUnit * 38)
        modal: true
        title: root.editor && root.editor.loaded
            ? i18n("Configure %1", root.editor.profileId)
            : i18n("Loading profile")
        standardButtons: QQC2.Dialog.NoButton

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.editor && root.editor.errorMessage.length > 0
                type: Kirigami.MessageType.Error
                text: root.editor
                    ? i18nc("error message followed by a stable diagnostic code", "%1 (code: %2)", root.editor.errorMessage, root.editor.errorCode)
                    : ""
            }
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.editor && root.editor.operationMessage.length > 0 && root.editor.errorMessage.length === 0
                type: Kirigami.MessageType.Positive
                text: root.editor ? root.editor.operationMessage : ""
            }
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.editor && root.editor.errorCode.endsWith(".RollbackIncomplete")
                type: Kirigami.MessageType.Warning
                text: i18n("Rollback was incomplete. Review the system log before running another backup.")
            }

            QQC2.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: editorDialog.availableWidth
                    spacing: Kirigami.Units.smallSpacing
                    enabled: root.editor && root.editor.loaded && !root.editor.busy

                    Kirigami.Heading { text: i18n("Profile"); level: 3 }
                    Kirigami.FormLayout {
                        Layout.fillWidth: true
                        QQC2.TextField {
                            Kirigami.FormData.label: i18n("Name:")
                            text: root.editor ? root.editor.name : ""
                            onEditingFinished: root.editor.setName(text)
                        }
                        QQC2.CheckBox {
                            Kirigami.FormData.label: i18n("Enabled:")
                            checked: root.editor ? root.editor.enabled : false
                            onToggled: if (root.editor) root.editor.setEnabled(checked)
                        }
                    }

                    Kirigami.Separator { Layout.fillWidth: true }
                    Kirigami.Heading { text: i18n("Target"); level: 3 }
                    Kirigami.FormLayout {
                        Layout.fillWidth: true
                        QQC2.TextField {
                            Kirigami.FormData.label: i18n("Device:")
                            text: root.editor ? root.editor.target.device || "" : ""
                            onEditingFinished: root.editor.setTargetValue("device", text)
                        }
                        QQC2.TextField {
                            Kirigami.FormData.label: i18n("LUKS UUID:")
                            text: root.editor ? root.editor.target.luksUuid || "" : ""
                            onEditingFinished: root.editor.setTargetValue("luksUuid", text)
                        }
                        QQC2.TextField {
                            Kirigami.FormData.label: i18n("Btrfs UUID:")
                            text: root.editor ? root.editor.target.btrfsUuid || "" : ""
                            onEditingFinished: root.editor.setTargetValue("btrfsUuid", text)
                        }
                        QQC2.TextField {
                            Kirigami.FormData.label: i18n("Mapper name:")
                            text: root.editor ? root.editor.target.mapperName || "" : ""
                            onEditingFinished: root.editor.setTargetValue("mapperName", text)
                        }
                        QQC2.ComboBox {
                            Kirigami.FormData.label: i18n("Unlock method:")
                            model: [i18n("Ask for password"), i18n("Key file")]
                            currentIndex: root.editor && root.editor.target.activation
                                && root.editor.target.activation.mode === "keyFile" ? 1 : 0
                            onActivated: root.editor.setTargetValue("activation.mode", currentIndex === 1 ? "keyFile" : "askPassword")
                        }
                        QQC2.TextField {
                            Kirigami.FormData.label: i18n("Key file:")
                            visible: root.editor && root.editor.target.activation
                                && root.editor.target.activation.mode === "keyFile"
                            text: visible ? root.editor.target.activation.keyFile || "" : ""
                            onEditingFinished: root.editor.setTargetValue("activation.keyFile", text)
                        }
                    }

                    Kirigami.Separator { Layout.fillWidth: true }
                    RowLayout {
                        Layout.fillWidth: true
                        Kirigami.Heading { Layout.fillWidth: true; text: i18n("Sources"); level: 3 }
                        QQC2.Button {
                            icon.name: "list-add"
                            text: i18n("Add source")
                            onClicked: root.editor.addSource()
                        }
                    }
                    Repeater {
                        model: root.editor ? root.editor.sources : []
                        delegate: ColumnLayout {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            RowLayout {
                                Layout.fillWidth: true
                                QQC2.TextField {
                                    Layout.fillWidth: true
                                    placeholderText: i18n("Source name")
                                    text: modelData.name || ""
                                    onEditingFinished: root.editor.setSourceValue(index, "name", text)
                                }
                                QQC2.Button {
                                    icon.name: "list-remove"
                                    display: QQC2.AbstractButton.IconOnly
                                    onClicked: root.editor.removeSource(index)
                                    QQC2.ToolTip.text: i18n("Remove source")
                                    QQC2.ToolTip.visible: hovered
                                }
                            }
                            Kirigami.FormLayout {
                                Layout.fillWidth: true
                                QQC2.TextField {
                                    Kirigami.FormData.label: i18n("Source ID:")
                                    text: modelData.id || ""
                                    onEditingFinished: root.editor.setSourceValue(index, "id", text)
                                }
                                QQC2.CheckBox {
                                    Kirigami.FormData.label: i18n("Enabled:")
                                    checked: modelData.enabled !== false
                                    onToggled: root.editor.setSourceValue(index, "enabled", checked)
                                }
                                QQC2.TextField {
                                    Kirigami.FormData.label: i18n("Subvolume:")
                                    text: modelData.subvolume || ""
                                    onEditingFinished: root.editor.setSourceValue(index, "subvolume", text)
                                }
                                QQC2.TextField {
                                    Kirigami.FormData.label: i18n("Snapshot directory:")
                                    text: modelData.localSnapshotDir || ""
                                    onEditingFinished: root.editor.setSourceValue(index, "localSnapshotDir", text)
                                }
                                QQC2.TextField {
                                    Kirigami.FormData.label: i18n("Target directory:")
                                    text: modelData.remoteSubdir || ""
                                    onEditingFinished: root.editor.setSourceValue(index, "remoteSubdir", text)
                                }
                                QQC2.SpinBox {
                                    Kirigami.FormData.label: i18n("Target retention:")
                                    from: 1; to: 100000
                                    value: modelData.remoteRetention || 30
                                    onValueModified: root.editor.setSourceValue(index, "remoteRetention", value)
                                }
                                QQC2.SpinBox {
                                    Kirigami.FormData.label: i18n("Local retention:")
                                    from: 1; to: 100000
                                    value: modelData.localRetention || 30
                                    onValueModified: root.editor.setSourceValue(index, "localRetention", value)
                                }
                            }
                            Kirigami.Separator { Layout.fillWidth: true }
                        }
                    }

                    Kirigami.Heading { text: i18n("Retention"); level: 3 }
                    Kirigami.FormLayout {
                        Layout.fillWidth: true
                        QQC2.SpinBox {
                            Kirigami.FormData.label: i18n("Target snapshots:")
                            from: 1; to: 100000
                            value: root.editor ? root.editor.settings.remoteRetention || 30 : 30
                            onValueModified: root.editor.setSettingValue("remoteRetention", value)
                        }
                        QQC2.SpinBox {
                            Kirigami.FormData.label: i18n("Local snapshots:")
                            from: 1; to: 100000
                            value: root.editor ? root.editor.settings.localRetention || 30 : 30
                            onValueModified: root.editor.setSettingValue("localRetention", value)
                        }
                    }
                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        visible: root.editor && root.editor.validationPreview.length > 0
                        type: Kirigami.MessageType.Information
                        text: i18n("Normalized configuration preview")
                    }
                    QQC2.TextArea {
                        Layout.fillWidth: true
                        visible: root.editor && root.editor.validationPreview.length > 0
                        readOnly: true
                        font.family: "monospace"
                        text: root.editor ? root.editor.validationPreview : ""
                        wrapMode: TextEdit.NoWrap
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                QQC2.Button {
                    icon.name: "tools-check-spelling"
                    text: i18n("Validate")
                    enabled: root.editor && root.editor.loaded && !root.editor.busy
                    onClicked: root.editor.validate()
                }
                QQC2.Button {
                    icon.name: "edit-copy"
                    text: i18n("Duplicate")
                    enabled: root.editor && root.editor.loaded && !root.editor.busy
                    onClicked: duplicateDialog.open()
                }
                QQC2.Button {
                    icon.name: "edit-delete"
                    text: i18n("Delete")
                    enabled: root.editor && root.editor.loaded && !root.editor.busy
                    onClicked: deleteDialog.open()
                }
                Item { Layout.fillWidth: true }
                QQC2.Button {
                    text: i18n("Discard")
                    enabled: root.editor && root.editor.dirty && !root.editor.busy
                    onClicked: root.editor.discard()
                }
                QQC2.Button {
                    icon.name: "document-save"
                    text: i18n("Apply")
                    highlighted: true
                    enabled: root.editor && root.editor.dirty && !root.editor.busy
                    onClicked: root.editor.save()
                }
                QQC2.Button { text: i18n("Close"); onClicked: editorDialog.close() }
            }
        }
    }

    QQC2.Dialog {
        id: conflictDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: i18n("Profile changed")
        standardButtons: QQC2.Dialog.NoButton
        contentItem: ColumnLayout {
            QQC2.Label {
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                text: i18n("The profile was changed by another process. Reloading discards this draft.")
                wrapMode: Text.Wrap
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                QQC2.Button { text: i18n("Keep draft"); onClicked: conflictDialog.close() }
                QQC2.Button {
                    text: i18n("Reload")
                    highlighted: true
                    onClicked: { conflictDialog.close(); root.editor.reload() }
                }
            }
        }
    }

    QQC2.Dialog {
        id: duplicateDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: i18n("Duplicate profile")
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        onAccepted: root.editor.duplicateAs(duplicateId.text)
        QQC2.TextField { id: duplicateId; placeholderText: i18n("New profile ID") }
    }

    QQC2.Dialog {
        id: deleteDialog
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: i18n("Delete profile")
        standardButtons: QQC2.Dialog.Yes | QQC2.Dialog.Cancel
        onAccepted: root.editor.deleteProfile()
        QQC2.Label { text: i18n("Delete this profile and its managed system configuration?") }
    }

    function statusText(state) {
        switch (state) {
        case "starting":
        case "running": return i18n("Backup is in progress")
        case "succeeded": return i18n("Backup completed successfully")
        case "failed": return i18n("Backup failed")
        case "cancelled": return i18n("Backup cancelled")
        default: return i18n("No active backup")
        }
    }

    function openEditorFor(profileId) {
        if (root.editor === null)
            return
        root.editor.load(profileId)
        editorDialog.open()
    }

    function targetStateText(state) {
        switch (state) {
        case "mounted": return i18n("Mounted")
        case "unexpected-mount": return i18n("Unexpected mount")
        case "unlocked": return i18n("Unlocked")
        case "connected": return i18n("Connected")
        case "disconnected": return i18n("Disconnected")
        default: return i18n("Unknown")
        }
    }

    function errorText(status) {
        return status.lastErrorCode.length > 0
            ? i18nc("error message followed by a stable diagnostic code", "%1 (code: %2)", status.lastError, status.lastErrorCode)
            : status.lastError
    }
}
