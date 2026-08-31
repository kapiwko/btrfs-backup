// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kcmutils as KCMUtils
import org.kde.ki18n as KI18n

KCMUtils.SimpleKCM {
    id: root

    required property var historyModel

    title: translations.i18n("History")

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    ProfileHistory {
        width: parent.width
        historyModel: root.historyModel
        statusTextFor: state => root.statusText(state)
    }

    function statusText(state) {
        switch (state) {
        case "starting":
        case "running":
            return translations.i18n("Backup is in progress");
        case "validating":
            return translations.i18n("Target validation is in progress");
        case "validated":
            return translations.i18n("Validation completed successfully");
        case "succeeded":
            return translations.i18n("Backup completed successfully");
        case "failed":
            return translations.i18n("Backup failed");
        case "cancelled":
            return translations.i18n("Backup cancelled");
        case "skipped":
            return translations.i18n("Backup skipped");
        default:
            return translations.i18n("No active backup");
        }
    }
}
