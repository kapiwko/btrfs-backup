// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtTest
import org.btrfsbackup.plasma as BtrfsBackup

TestCase {
    name: "ProfileStatusBadge"
    when: true

    function status(overrides) {
        const result = {
            lastError: "",
            configurationValid: true,
            run: {state: "idle"},
            target: {
                connected: true,
                spaceBelowMinimum: false,
                mounted: false,
                unlocked: false
            }
        }
        for (const name in overrides)
            result[name] = overrides[name]
        return result
    }

    function test_priority() {
        compare(BtrfsBackup.ProfileStatusBadge.icon(status({target: {connected: true, unlocked: true}})),
                "emblem-unlocked")
        compare(BtrfsBackup.ProfileStatusBadge.icon(status({target: {connected: true, spaceBelowMinimum: true}})),
                "emblem-warning")
        compare(BtrfsBackup.ProfileStatusBadge.icon(status({target: {connected: false}})),
                "emblem-unavailable")
        compare(BtrfsBackup.ProfileStatusBadge.icon(status({lastError: "failed", target: {connected: false}})),
                "emblem-error")
    }

    function test_attentionOnlyReportsActionableStates() {
        compare(BtrfsBackup.ProfileStatusBadge.attentionIcon(status({target: {connected: true, mounted: true}})), "")
        compare(BtrfsBackup.ProfileStatusBadge.attentionIcon(status({target: {connected: true, unlocked: true}})), "")
        compare(BtrfsBackup.ProfileStatusBadge.attentionIcon(status({target: {connected: true, spaceBelowMinimum: true}})),
                "emblem-warning")
        compare(BtrfsBackup.ProfileStatusBadge.attentionIcon(status({profileEnabled: true, target: {connected: false}})),
                "emblem-unavailable")
        compare(BtrfsBackup.ProfileStatusBadge.attentionIcon(status({lastError: "failed"})), "emblem-error")
    }

    function test_mostImportantProfileAttentionWins() {
        compare(BtrfsBackup.ProfileStatusBadge.mostImportantAttention({
            healthy: {attentionPriority: 99, attentionIcon: ""},
            warning: {attentionPriority: 2, attentionIcon: "emblem-warning"},
            error: {attentionPriority: 1, attentionIcon: "emblem-error"}
        }), "emblem-error")
    }
}
