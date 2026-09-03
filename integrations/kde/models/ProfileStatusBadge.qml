// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma Singleton

import QtQml

QtObject {
    function icon(profileStatus) {
        if ((profileStatus?.lastError?.length ?? 0) > 0
                || !(profileStatus?.configurationValid ?? false)
                || (profileStatus?.run?.state ?? "") === "failed")
            return "emblem-error"
        if (!(profileStatus?.target?.connected ?? false))
            return "emblem-unavailable"
        if (profileStatus?.target?.spaceBelowMinimum ?? false)
            return "emblem-warning"
        if (profileStatus?.target?.mounted ?? false)
            return "emblem-success"
        if (profileStatus?.target?.unlocked ?? false)
            return "emblem-unlocked"
        return "emblem-locked"
    }

    function attentionPriority(profileStatus) {
        if ((profileStatus?.lastError?.length ?? 0) > 0
                || !(profileStatus?.configurationValid ?? false)
                || (profileStatus?.run?.state ?? "") === "failed")
            return 1
        if (profileStatus?.target?.spaceBelowMinimum ?? false)
            return 2
        if ((profileStatus?.profileEnabled ?? false)
                && !(profileStatus?.target?.connected ?? false))
            return 3
        return 99
    }

    function attentionIcon(profileStatus) {
        switch (attentionPriority(profileStatus)) {
        case 1: return "emblem-error"
        case 2: return "emblem-warning"
        case 3: return "emblem-unavailable"
        default: return ""
        }
    }

    function mostImportantAttention(summaries) {
        let selected = null
        for (const profileId in summaries) {
            const summary = summaries[profileId]
            if ((summary.attentionIcon ?? "").length > 0
                    && (selected === null || summary.attentionPriority < selected.attentionPriority))
                selected = summary
        }
        return selected?.attentionIcon ?? ""
    }
}
