// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "RestoreSuccess"
    when: windowShown
    width: 620
    height: 430

    property var page

    QtObject {
        id: controller
        property url sourceUrl: "btrfsbackup:/home/snapshot/Documents"
        property string sourceName: "Documents"
        property string destination: "/tmp/restored/Documents"
        property string sourceType: "Folder"
        property string sourceSize: "Calculated during restore"
        property string sourceModified: "05.09.2026 12:00"
        property string snapshotCreated: "05.09.2026 12:05"
        property bool sourceDetailsAvailable: true
        property bool replaceExisting: false
        property string planSummary: ""
        property string errorText: ""
        property string errorCode: ""
        property string errorTechnicalDetails: ""
        property bool busy: false
        property bool completed: true
        property int restoredFiles: 482
        property int restoredBytes: 1932735283
        property string restoredSize: "1.8 GiB"
        property int openRequests: 0
        signal overwriteConfirmationRequested(string destination)
        signal stateChanged()
        function preview() { return true }
        function chooseDestination() {}
        function loadDetails() {}
        function confirmOverwrite() { return true }
        function execute() {}
        function cancel() {}
        function openRestoredDirectory() { ++openRequests }
    }

    SignalSpy {
        id: closeSpy
        target: testCase.page
        signalName: "closeRequested"
    }

    function initTestCase() {
        const component = Qt.createComponent("../RestorePage.qml")
        compare(component.status, Component.Ready, component.errorString())
        page = component.createObject(testCase, {controller: controller, width: 620, height: 430})
        verify(page !== null)
    }

    function cleanupTestCase() {
        if (page !== undefined && page !== null)
            page.destroy()
    }

    function test_completedRestoreShowsOutcomeAndActions() {
        const title = findChild(page, "restoreSuccessTitle")
        const details = findChild(page, "restoreSuccessDetails")
        const openButton = findChild(page, "openRestoredDirectoryButton")
        const closeButton = findChild(page, "closeRestoreButton")

        compare(page.controller.completed, true)
        verify(title.text.indexOf("482") !== -1)
        verify(details.text.indexOf("1.8 GiB") !== -1)
        verify(details.text.indexOf(controller.destination) !== -1)

        openButton.clicked()
        compare(controller.openRequests, 1)
        closeButton.clicked()
        compare(closeSpy.count, 1)
    }

    function test_restoreFormContainsDetailsAndBoundsLongDestination() {
        controller.completed = false
        controller.destination = "/home/user/Downloads/a_very_long_restore_destination_that_must_not_expand_the_window.csv"
        wait(0)

        const sourceName = findChild(page, "restoreSourceName")
        const sourceDetails = findChild(page, "restoreSourceDetails")
        const destinationField = findChild(page, "restoreDestinationField")
        const destinationButton = findChild(page, "chooseRestoreDestinationButton")
        compare(sourceName.text, controller.sourceName)
        verify(sourceDetails !== null)
        compare(destinationField.text, controller.destination)
        verify(destinationField.x + destinationField.width <= page.width)
        verify(destinationButton.x + destinationButton.width <= page.width)
    }
}
