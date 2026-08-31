// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

RowLayout {
    id: header

    required property string text

    spacing: Kirigami.Units.smallSpacing

    PlasmaComponents3.Label {
        text: header.text
        font.weight: Font.DemiBold
    }

    Kirigami.Separator {
        Layout.fillWidth: true
    }
}
