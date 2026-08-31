// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

RowLayout {
    id: profileActions

    required property var ejectAction
    required property var browseAction
    required property bool browseVisible

    spacing: Kirigami.Units.smallSpacing

    PlasmaComponents3.ToolButton {
        Layout.fillWidth: true
        action: profileActions.ejectAction
    }

    PlasmaComponents3.ToolButton {
        Layout.fillWidth: true
        visible: profileActions.browseVisible
        action: profileActions.browseAction
    }
}
