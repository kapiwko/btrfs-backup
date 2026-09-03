// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtTest
import org.btrfsbackup.kde as BtrfsBackup

TestCase {
    name: "ProfilePresentation"
    when: true

    function status(overrides) {
        const result = {
            lastError: "",
            configurationValid: true,
            managerConnected: true,
            operationPending: false,
            browseSupported: true,
            profileEnabled: true,
            run: {state: "idle", canCancel: false},
            target: {
                connected: true,
                spaceBelowMinimum: false,
                mounted: false,
                unlocked: false,
                safeToRemove: false
            }
        }
        for (const name in overrides)
            result[name] = overrides[name]
        return result
    }

    function test_priority() {
        compare(BtrfsBackup.ProfilePresentation.statusIcon(status({target: {connected: true, unlocked: true}})),
                "emblem-unlocked")
        compare(BtrfsBackup.ProfilePresentation.statusIcon(status({target: {connected: true, spaceBelowMinimum: true}})),
                "emblem-warning")
        compare(BtrfsBackup.ProfilePresentation.statusIcon(status({target: {connected: false}})),
                "emblem-unavailable")
        compare(BtrfsBackup.ProfilePresentation.statusIcon(status({lastError: "failed", target: {connected: false}})),
                "emblem-error")
    }

    function test_attentionOnlyReportsActionableStates() {
        compare(BtrfsBackup.ProfilePresentation.attentionIcon(status({target: {connected: true, mounted: true}})), "")
        compare(BtrfsBackup.ProfilePresentation.attentionIcon(status({target: {connected: true, unlocked: true}})), "")
        compare(BtrfsBackup.ProfilePresentation.attentionIcon(status({target: {connected: true, spaceBelowMinimum: true}})),
                "emblem-warning")
        compare(BtrfsBackup.ProfilePresentation.attentionIcon(status({profileEnabled: true, target: {connected: false}})),
                "emblem-unavailable")
        compare(BtrfsBackup.ProfilePresentation.attentionIcon(status({lastError: "failed"})), "emblem-error")
    }

    function test_mostImportantProfileAttentionWins() {
        compare(BtrfsBackup.ProfilePresentation.mostImportantAttention({
            healthy: {attentionPriority: 99, attentionIcon: ""},
            warning: {attentionPriority: 2, attentionIcon: "emblem-warning"},
            error: {attentionPriority: 1, attentionIcon: "emblem-error"}
        }), "emblem-error")
    }

    function test_actionAvailability() {
        const unlocked = status({target: {connected: true, unlocked: true, mounted: false}})
        verify(BtrfsBackup.ProfilePresentation.canStart(unlocked))
        verify(BtrfsBackup.ProfilePresentation.canBrowse(unlocked))
        verify(BtrfsBackup.ProfilePresentation.canEject(unlocked))
        verify(BtrfsBackup.ProfilePresentation.canToggleAutomatic(unlocked))

        const running = status({run: {state: "running", canCancel: true}})
        verify(!BtrfsBackup.ProfilePresentation.canStart(running))
        verify(BtrfsBackup.ProfilePresentation.canCancel(running))
        verify(!BtrfsBackup.ProfilePresentation.canEject(running))

        const pending = status({operationPending: true})
        verify(!BtrfsBackup.ProfilePresentation.primaryActionEnabled(pending))
        verify(!BtrfsBackup.ProfilePresentation.canBrowse(pending))
        verify(!BtrfsBackup.ProfilePresentation.canToggleAutomatic(pending))

        const disconnected = status({target: {connected: false}})
        verify(!BtrfsBackup.ProfilePresentation.canStart(disconnected))
        verify(!BtrfsBackup.ProfilePresentation.canBrowse(disconnected))
        verify(!BtrfsBackup.ProfilePresentation.canEject(disconnected))
    }

    function test_sharedIconsAndPriority() {
        compare(BtrfsBackup.ProfilePresentation.targetStateIcon("unlocked"), "object-unlocked-symbolic")
        compare(BtrfsBackup.ProfilePresentation.deviceConnectionIcon(false), "network-disconnect-symbolic")
        compare(BtrfsBackup.ProfilePresentation.historyStateIcon("succeeded"), "dialog-positive")
        compare(BtrfsBackup.ProfilePresentation.summaryPriority(status({run: {state: "running"}})), 2)
        compare(BtrfsBackup.ProfilePresentation.summaryPriority(status({target: {spaceBelowMinimum: true}})), 3)
    }
}
