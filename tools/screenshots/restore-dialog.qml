// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window

Window {
    id: runner

    width: 620
    height: 260
    visible: true
    title: "Btrfs Backup Restore"

    Component.onCompleted: requestActivate()

    QtObject {
        id: fakeController

        property string sourceName: "report.odt"
        property string destination: "/home/kamil/report.odt"
        property bool replaceExisting: false
        property string planSummary: "Ready to restore one file (2.8 MiB) from the selected snapshot."
        property string errorText: ""
        property bool busy: false

        function preview() {}
        function execute() {}
        function cancel() {}
    }

    Loader {
        id: restoreLoader

        anchors.fill: parent
        asynchronous: true
        Component.onCompleted: setSource(
            Qt.resolvedUrl("../../integrations/kde/restore/RestorePage.qml"),
            {
                "controller": fakeController,
                "destinationDialogEnabled": false
            }
        )
    }

    Timer {
        interval: 100
        running: true
        repeat: true
        onTriggered: {
            if (restoreLoader.status === Loader.Error)
                Qt.exit(2)
            if (restoreLoader.status !== Loader.Ready || !restoreLoader.item)
                return

            stop()
            runner.contentItem.grabToImage(function(result) {
                if (!result.saveToFile("restore-dialog.png"))
                    Qt.exit(2)
                else
                    runner.requestActivate()
            }, Qt.size(runner.width, runner.height))
        }
    }

    Timer {
        interval: 5000
        running: true
        repeat: false
        onTriggered: Qt.exit(0)
    }
}
