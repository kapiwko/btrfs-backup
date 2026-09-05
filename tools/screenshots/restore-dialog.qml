// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami
import "../../integrations/kde/restore" as Restore

Kirigami.ApplicationWindow {
    id: runner

    width: 620
    height: 430
    minimumWidth: 420
    minimumHeight: 360
    visible: true
    title: "Restore report.odt"

    Component.onCompleted: requestActivate()

    KI18n.KI18nContext {
        translationDomain: "btrfs-backup-kde-restore"
    }

    QtObject {
        id: fakeController

        property string sourceName: "report.odt"
        property string destination: "/home/kamil/report.odt"
        property bool replaceExisting: false
        property string planSummary: "Plan ready: restore report.odt (2.8 MiB) from the Sep 2, 2026 snapshot, preserve its metadata and verify the result."
        property string errorText: ""
        property string errorCode: ""
        property string errorTechnicalDetails: ""
        property bool busy: false
        property bool completed: false
        property int restoredFiles: 0
        property int restoredBytes: 0
        property string restoredSize: "0 B"

        signal overwriteConfirmationRequested(string destination)
        signal stateChanged()

        function preview() {}
        function chooseDestination() {}
        function execute() {}
        function cancel() {}
        function confirmOverwrite() {}
        function openRestoredDirectory() {}
    }

    pageStack.initialPage: Restore.RestorePage {
        controller: fakeController
    }
}
