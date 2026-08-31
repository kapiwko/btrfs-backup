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
    property bool expanded: false

    spacing: Kirigami.Units.smallSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    QQC2.ToolButton {
        Layout.fillWidth: true
        text: translations.i18n("Technical details")
        icon.name: root.expanded ? "arrow-down-symbolic" : "arrow-right-symbolic"
        display: QQC2.AbstractButton.TextBesideIcon
        onClicked: root.expanded = !root.expanded
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        visible: root.expanded

        SelectableLabel {
            Kirigami.FormData.label: translations.i18n("Profile ID:")
            text: root.editor?.profileId ?? ""
        }
        SelectableLabel {
            Kirigami.FormData.label: translations.i18n("Schema version:")
            text: String(root.editor?.schemaVersion ?? "")
        }
        SelectableLabel {
            Kirigami.FormData.label: translations.i18n("Generation:")
            text: root.editor?.generation || translations.i18n("Unknown")
        }
        SelectableLabel {
            Kirigami.FormData.label: translations.i18n("Fingerprint:")
            text: root.editor?.fingerprint || translations.i18n("Unknown")
        }
        SelectableLabel {
            Kirigami.FormData.label: translations.i18n("Device:")
            text: root.editor?.target?.device || translations.i18n("Unknown")
        }
        SelectableLabel {
            Kirigami.FormData.label: translations.i18n("LUKS UUID:")
            text: root.editor?.target?.luksUuid || translations.i18n("Unknown")
        }
        SelectableLabel {
            Kirigami.FormData.label: translations.i18n("Btrfs UUID:")
            text: root.editor?.target?.btrfsUuid || translations.i18n("Unknown")
        }
        SelectableLabel {
            Kirigami.FormData.label: translations.i18n("Partition UUID:")
            text: root.editor?.target?.partitionUuid || translations.i18n("Not recorded")
        }
        SelectableLabel {
            Kirigami.FormData.label: translations.i18n("Mapper:")
            text: root.editor?.target?.mapperName || translations.i18n("Unknown")
        }
        SelectableLabel {
            Kirigami.FormData.label: translations.i18n("Repository:")
            text: root.editor?.paths?.remoteRoot || translations.i18n("Unknown")
        }
        SelectableLabel {
            Kirigami.FormData.label: translations.i18n("Incoming data:")
            text: root.editor?.paths?.incomingRoot || translations.i18n("Unknown")
        }
    }

    component SelectableLabel: QQC2.Label {
        Layout.fillWidth: true
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
        font.family: "monospace"
    }
}
