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
