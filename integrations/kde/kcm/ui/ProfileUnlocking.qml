// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

Item {
    id: root

    required property var editor
    readonly property var methods: root.editor?.target?.activation ? [root.editor.target.activation] : []

    implicitHeight: Math.ceil(methodList.count > 0 ? methodList.contentHeight : (methodList.headerItem?.implicitHeight ?? 0) + methodList.emptyContentHeight)
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
    }

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    ListView {
        id: methodList

        readonly property real emptyContentHeight: Kirigami.Units.gridUnit * 7

        anchors.fill: parent
        model: root.methods
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        clip: false

        header: Kirigami.InlineViewHeader {
            width: methodList.width
            text: translations.i18n("Unlocking")
        }

        delegate: QQC2.ItemDelegate {
            id: methodRow

            required property var modelData

            width: ListView.view?.width ?? implicitWidth
            hoverEnabled: false
            focusPolicy: Qt.NoFocus
            Kirigami.Theme.useAlternateBackgroundColor: true

            contentItem: Kirigami.TitleSubtitleWithActions {
                title: root.activationTitle(methodRow.modelData.mode ?? "")
                subtitle: translations.i18n("Automatic activation policy")
                selected: false
                actions: []
            }
        }

        Kirigami.PlaceholderMessage {
            width: parent.width - Kirigami.Units.largeSpacing * 4
            anchors.horizontalCenter: parent.horizontalCenter
            y: (methodList.headerItem?.height ?? 0) + Kirigami.Units.largeSpacing * 2
            visible: methodList.count === 0
            icon.name: "lock-symbolic"
            text: translations.i18n("No unlock method configured")
        }
    }

    function activationTitle(mode) {
        switch (mode) {
        case "keyFile":
            return translations.i18n("Key file");
        case "askPassword":
            return translations.i18n("Ask for a password");
        default:
            return translations.i18n("Unknown unlock method");
        }
    }
}
