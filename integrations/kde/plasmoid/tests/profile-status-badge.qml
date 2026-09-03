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
}
