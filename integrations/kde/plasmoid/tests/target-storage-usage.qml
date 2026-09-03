// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Window 2.15
import org.btrfsbackup.kde 1.0
import "../package/contents/ui" as BackupUi

Window {
    width: 260
    height: 320
    visible: false

    BackupUi.TargetStorageUsage {
        id: usage
        width: 260
        height: 320
        supported: true
        known: true
        capacityText: "3,6 TiB"
        usedText: "1,2 TiB"
        availableText: "2,5 TiB"
        usagePercent: 32
        live: false
        measuredAt: "2026-08-30T12:34:56Z"
        relativeMeasurementTime: "Just now"
        belowMinimum: true
    }

    Timer {
        interval: 100
        running: true
        repeat: false
        onTriggered: {
            if (!usage.visible
                    || usage.capacityText !== "3,6 TiB"
                    || usage.usedText !== "1,2 TiB"
                    || usage.availableText !== "2,5 TiB"
                    || usage.usageText !== "32%"
                    || usage.measuredAt !== "2026-08-30T12:34:56Z") {
                Qt.exit(2)
                return
            }
            usage.known = false
            if (!usage.visible) {
                Qt.exit(3)
                return
            }
            usage.supported = false
            if (usage.visible) {
                Qt.exit(4)
                return
            }
            Qt.exit(0)
        }
    }
}
