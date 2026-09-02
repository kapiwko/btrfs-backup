// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

QQC2.ScrollView {
    id: root

    required property var editor
    required property string profileId
    required property var profileStatus
    required property var statusTextFor
    required property var targetStateTextFor
    required property var runningStateFor
    property var credentialModel: null

    signal addSourceRequested(string name, string subvolume, int localRetention, int targetRetention)
    signal editSourceRequested(int index, string name, int localRetention, int targetRetention)
    signal removeSourceRequested(int index, var source)
    signal deleteRequested

    clip: true
    contentWidth: availableWidth

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    ColumnLayout {
        x: Kirigami.Units.largeSpacing
        width: Math.max(0, root.availableWidth - Kirigami.Units.largeSpacing * 2)
        spacing: Kirigami.Units.largeSpacing
        enabled: root.editor !== null && root.editor.loaded

        Item {
            Layout.fillWidth: true
            implicitHeight: Kirigami.Units.largeSpacing
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.editor !== null && root.editor.errorCode.endsWith(".RollbackIncomplete")
            type: Kirigami.MessageType.Warning
            text: translations.i18n("Rollback was incomplete. Review the system log before running another backup.")
        }

        ProfileOverview {
            Layout.fillWidth: true
            editor: root.editor
            profileStatus: root.profileStatus
            statusTextFor: root.statusTextFor
            targetStateTextFor: root.targetStateTextFor
            runningStateFor: root.runningStateFor
        }

        ProfileSources {
            Layout.fillWidth: true
            Layout.leftMargin: -Kirigami.Units.largeSpacing
            Layout.rightMargin: -Kirigami.Units.largeSpacing
            editor: root.editor
            onAddRequested: (name, subvolume, localRetention, targetRetention) => {
                root.addSourceRequested(name, subvolume, localRetention, targetRetention);
            }
            onEditRequested: (index, name, localRetention, targetRetention) => {
                root.editSourceRequested(index, name, localRetention, targetRetention);
            }
            onRemoveRequested: (index, source) => root.removeSourceRequested(index, source)
        }

        ProfileUnlocking {
            Layout.fillWidth: true
            Layout.leftMargin: -Kirigami.Units.largeSpacing
            Layout.rightMargin: -Kirigami.Units.largeSpacing
            editor: root.editor
            profileId: root.profileId
            credentialModel: root.credentialModel
        }

        ProfileBehavior {
            Layout.fillWidth: true
            editor: root.editor
        }

        TechnicalDetails {
            Layout.fillWidth: true
            editor: root.editor
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true
            }
            QQC2.Button {
                icon.name: "edit-delete-symbolic"
                text: translations.i18n("Delete profile")
                enabled: root.editor !== null && !root.editor.busy
                onClicked: root.deleteRequested()
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: Kirigami.Units.largeSpacing
        }
    }
}
