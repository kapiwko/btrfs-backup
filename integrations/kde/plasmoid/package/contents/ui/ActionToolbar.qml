// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

RowLayout {
    id: root

    required property bool managerConnected
    required property bool targetConnected
    required property bool running
    required property bool operationPending
    required property bool canCancel
    required property bool canEject

    signal startRequested()
    signal cancelRequested()
    signal validationRequested()
    signal ejectRequested()
    signal refreshRequested()

    spacing: Kirigami.Units.smallSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "plasma_applet_org.btrfsbackup.plasmoid"
    }

    PlasmaComponents3.ToolButton {
        text: translations.i18n("Start backup")
        display: PlasmaComponents3.AbstractButton.IconOnly
        icon.name: "media-playback-start"
        visible: root.managerConnected && root.targetConnected && !root.running
        enabled: !root.operationPending
        onClicked: root.startRequested()
        QQC2.ToolTip.text: text
        QQC2.ToolTip.visible: hovered
    }

    PlasmaComponents3.ToolButton {
        text: translations.i18n("Cancel")
        display: PlasmaComponents3.AbstractButton.IconOnly
        icon.name: "process-stop"
        visible: root.running
        enabled: root.canCancel && !root.operationPending
        onClicked: root.cancelRequested()
        QQC2.ToolTip.text: text
        QQC2.ToolTip.visible: hovered
    }

    Item { Layout.fillWidth: true }

    PlasmaComponents3.ToolButton {
        text: translations.i18n("Validate")
        display: PlasmaComponents3.AbstractButton.IconOnly
        icon.name: "task-complete"
        visible: root.managerConnected && root.targetConnected && !root.running
        enabled: !root.operationPending
        onClicked: root.validationRequested()
        QQC2.ToolTip.text: text
        QQC2.ToolTip.visible: hovered
    }

    PlasmaComponents3.ToolButton {
        text: translations.i18n("Eject")
        display: PlasmaComponents3.AbstractButton.IconOnly
        icon.name: "media-eject"
        visible: root.managerConnected && root.targetConnected && !root.running && root.canEject
        enabled: !root.operationPending
        onClicked: root.ejectRequested()
        QQC2.ToolTip.text: text
        QQC2.ToolTip.visible: hovered
    }

    PlasmaComponents3.ToolButton {
        text: translations.i18n("Refresh")
        display: PlasmaComponents3.AbstractButton.IconOnly
        icon.name: "view-refresh"
        enabled: !root.operationPending
        onClicked: root.refreshRequested()
        QQC2.ToolTip.text: text
        QQC2.ToolTip.visible: hovered
    }
}
