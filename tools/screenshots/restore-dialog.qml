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
        property string sourceType: "File"
        property string sourceIcon: "x-office-document"
        property bool sourceIsDirectory: false
        property string sourceSize: "2.8 MiB"
        property string sourceModified: "02.09.2026 16:42"
        property string snapshotCreated: "02.09.2026 19:00"
        property bool sourceDetailsAvailable: true
        property string destination: "/home/kamil/report.odt"
        property bool replaceExisting: false
        property string errorText: ""
        property string errorCode: ""
        property string errorTechnicalDetails: ""
        property bool busy: false
        property bool completed: false
        property int restoredFiles: 0
        property int restoredBytes: 0
        property string restoredSize: "0 B"
        property real progress: -1
        property string transferredSize: "0 B"
        property string transferSpeed: ""
        property string currentItem: "report.odt"

        signal overwriteConfirmationRequested(string destination)
        signal stateChanged()

        function loadDetails() {}
        function chooseDestination() {}
        function execute() {}
        function cancel() {}
        function confirmOverwrite() {}
        function openRestoredLocation() {}
    }

    pageStack.initialPage: Restore.RestorePage {
        controller: fakeController
    }
}
