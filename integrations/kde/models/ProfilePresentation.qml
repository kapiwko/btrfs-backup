// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma Singleton

import QtQml

QtObject {
    function hasFailure(profileStatus) {
        return (profileStatus?.lastError?.length ?? 0) > 0
            || !(profileStatus?.configurationValid ?? false)
            || (profileStatus?.run?.state ?? "") === "failed"
    }

    function isRunning(state) {
        return state === "starting" || state === "running" || state === "validating"
    }

    function statusIcon(profileStatus) {
        if (hasFailure(profileStatus))
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
        if (hasFailure(profileStatus))
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

    function sortedAttentionSummaries(summaries) {
        const selected = []
        for (const profileId in summaries) {
            const summary = summaries[profileId]
            if ((summary.attentionIcon ?? "").length > 0)
                selected.push(summary)
        }
        selected.sort((left, right) => {
            if (left.attentionPriority !== right.attentionPriority)
                return left.attentionPriority - right.attentionPriority
            return String(left.profileName ?? "").localeCompare(String(right.profileName ?? ""))
        })
        return selected
    }

    function summaryPriority(profileStatus) {
        if (hasFailure(profileStatus))
            return 1
        if (isRunning(profileStatus?.run?.state ?? ""))
            return 2
        if (profileStatus?.target?.spaceBelowMinimum ?? false)
            return 3
        if (profileStatus?.target?.safeToRemove ?? false)
            return 5
        if ((profileStatus?.run?.state ?? "") === "succeeded")
            return 6
        return 7
    }

    function canStart(profileStatus) {
        return (profileStatus?.managerConnected ?? false)
            && !(profileStatus?.operationPending ?? false)
            && !isRunning(profileStatus?.run?.state ?? "")
            && (profileStatus?.target?.connected ?? false)
    }

    function canCancel(profileStatus) {
        return (profileStatus?.managerConnected ?? false)
            && !(profileStatus?.operationPending ?? false)
            && isRunning(profileStatus?.run?.state ?? "")
            && (profileStatus?.run?.canCancel ?? false)
    }

    function canBrowse(profileStatus) {
        return (profileStatus?.browseSupported ?? false)
            && (profileStatus?.managerConnected ?? false)
            && (profileStatus?.target?.connected ?? false)
            && !(profileStatus?.operationPending ?? false)
    }

    function canEject(profileStatus) {
        return (profileStatus?.managerConnected ?? false)
            && (profileStatus?.target?.connected ?? false)
            && !(profileStatus?.operationPending ?? false)
            && !isRunning(profileStatus?.run?.state ?? "")
            && ((profileStatus?.target?.mounted ?? false)
                || (profileStatus?.target?.unlocked ?? false))
    }

    function canToggleAutomatic(profileStatus) {
        return (profileStatus?.managerConnected ?? false)
            && !(profileStatus?.operationPending ?? false)
    }

    function primaryActionVisible(profileStatus) {
        return (profileStatus?.managerConnected ?? false)
            && (isRunning(profileStatus?.run?.state ?? "")
                || (profileStatus?.target?.connected ?? false))
    }

    function primaryActionEnabled(profileStatus) {
        return isRunning(profileStatus?.run?.state ?? "")
            ? canCancel(profileStatus)
            : canStart(profileStatus)
    }

    function targetStateIcon(state) {
        switch (state) {
        case "mounted": return "drive-harddisk-root-symbolic"
        case "unexpected-mount": return "dialog-warning-symbolic"
        case "unlocked": return "object-unlocked-symbolic"
        case "connected": return "object-locked-symbolic"
        case "disconnected": return "network-disconnect-symbolic"
        default: return "dialog-question-symbolic"
        }
    }

    function deviceConnectionIcon(connected) {
        return connected ? "drive-removable-media-symbolic" : "network-disconnect-symbolic"
    }

    function historyStateIcon(state) {
        switch (state) {
        case "succeeded": return "dialog-positive"
        case "failed": return "dialog-error"
        case "cancelled": return "dialog-cancel"
        default: return "dialog-information"
        }
    }
}
