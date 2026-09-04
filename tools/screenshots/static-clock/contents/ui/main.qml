// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.plasma.plasmoid

PlasmoidItem {
    implicitWidth: 76
    implicitHeight: 44
    preferredRepresentation: fullRepresentation

    fullRepresentation: Item {
        implicitWidth: 76
        implicitHeight: 44

        Column {
            anchors.centerIn: parent
            spacing: -2

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: Kirigami.Theme.textColor
                font.pixelSize: 17
                text: "12:14"
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: Kirigami.Theme.textColor
                font.pixelSize: 11
                text: "03.09.2026"
            }
        }
    }
}
