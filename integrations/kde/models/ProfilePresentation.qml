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
        if (profileStatus?.target?.spaceBelowMinimum ?? false)
            return "emblem-warning"
        return ""
    }

    function attentionPriority(profileStatus) {
        if (hasFailure(profileStatus))
            return 1
        if (profileStatus?.target?.spaceBelowMinimum ?? false)
            return 2
        return 99
    }

    function attentionIcon(profileStatus) {
        switch (attentionPriority(profileStatus)) {
        case 1: return "emblem-error"
        case 2: return "emblem-warning"
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

    function statusText(translations, state) {
        switch (state) {
        case "starting":
        case "running": return translations.i18n("Backup is in progress")
        case "validating": return translations.i18n("Target validation is in progress")
        case "validated": return translations.i18n("Validation completed successfully")
        case "succeeded": return translations.i18n("Backup completed successfully")
        case "failed": return translations.i18n("Backup failed")
        case "cancelled": return translations.i18n("Backup cancelled")
        case "skipped": return translations.i18n("Backup skipped")
        default: return translations.i18n("No active backup")
        }
    }

    function configurationErrorText(translations, code) {
        switch (code) {
        case "configuration.source_missing":
            return translations.i18n("A configured source subvolume does not exist.")
        case "configuration.source_not_subvolume":
            return translations.i18n("A configured source path is not a Btrfs subvolume.")
        default:
            return translations.i18n("A configured source subvolume cannot be inspected.")
        }
    }

    function targetStateText(translations, state, safeToRemove) {
        switch (state) {
        case "mounted": return translations.i18n("Mounted")
        case "unexpected-mount": return translations.i18n("Unexpected mount")
        case "unlocked": return translations.i18n("Unlocked")
        case "connected": return safeToRemove
            ? translations.i18n("Safe to remove")
            : translations.i18n("Connected")
        case "disconnected": return translations.i18n("Disconnected")
        default: return translations.i18n("Unknown")
        }
    }

    function phaseText(translations, phase) {
        switch (phase) {
        case "run-started": return translations.i18n("Starting backup")
        case "source-started": return translations.i18n("Preparing backup source")
        case "recover-pending": return translations.i18n("Recovering interrupted backup")
        case "cleanup-incoming": return translations.i18n("Cleaning temporary data")
        case "before-snapshot-hook": return translations.i18n("Running pre-snapshot hooks")
        case "create-snapshot": return translations.i18n("Creating local snapshot")
        case "after-snapshot-hook": return translations.i18n("Running post-snapshot hooks")
        case "send-receive": return translations.i18n("Preparing data transfer")
        case "sizing": return translations.i18n("Calculating transfer size")
        case "transferring": return translations.i18n("Transferring backup data")
        case "verify-received": return translations.i18n("Verifying received snapshot")
        case "commit-received": return translations.i18n("Committing received snapshot")
        case "apply-remote-retention": return translations.i18n("Applying target retention")
        case "apply-local-retention": return translations.i18n("Applying local retention")
        case "cleanup-source": return translations.i18n("Cleaning backup source")
        case "source-completed": return translations.i18n("Finalizing backup")
        case "validating-target": return translations.i18n("Validating backup target")
        case "validated": return translations.i18n("Validation completed successfully")
        default: return translations.i18n("Preparing backup")
        }
    }

    function activityText(translations, activity, phase) {
        switch (activity) {
        case "sizing": return translations.i18n("Calculating transfer size")
        case "transferring": return translations.i18n("Transferring backup data")
        default: return phaseText(translations, phase)
        }
    }

    function operationResultText(translations, operation, profileEnabled) {
        switch (operation) {
        case "start-backup": return translations.i18n("Backup started")
        case "cancel-backup": return translations.i18n("Cancellation requested")
        case "validate-target": return translations.i18n("Validation completed successfully")
        case "eject-target": return translations.i18n("Target ejected safely")
        case "profile-activation": return profileEnabled
            ? translations.i18n("Automatic backups enabled")
            : translations.i18n("Automatic backups disabled")
        default: return translations.i18n("Operation completed")
        }
    }

    function sourceText(translations, run) {
        const name = run.sourceName || translations.i18n("Unknown")
        if (run.sourceIndex <= 0 || run.sourceCount <= 0)
            return name
        return translations.i18n("%1 (%2 of %3)", name, run.sourceIndex, run.sourceCount)
    }

    function formatDuration(translations, value) {
        const seconds = Number(value)
        if (seconds < 0)
            return translations.i18n("Unknown")
        const hours = Math.floor(seconds / 3600)
        const minutes = Math.floor((seconds % 3600) / 60)
        const remainder = Math.floor(seconds % 60)
        if (hours > 0)
            return translations.i18n("%1 h %2 min", hours, minutes)
        if (minutes > 0)
            return translations.i18n("%1 min %2 sec", minutes, remainder)
        return translations.i18np("1 second", "%1 seconds", Math.max(1, Math.floor(seconds)))
    }

    function formatEta(translations, value) {
        let seconds = Number(value ?? -1)
        if (seconds < 0)
            return translations.i18n("Unknown")
        const minutes = Math.floor(seconds / 60)
        seconds = Math.floor(seconds % 60)
        if (minutes > 0)
            return translations.i18n("%1 min %2 sec", minutes, seconds)
        return translations.i18n("%1 sec", seconds)
    }

    function relativeTime(translations, value, revision) {
        revision
        const timestamp = Date.parse(value)
        if (isNaN(timestamp))
            return translations.i18n("Unknown")
        const seconds = Math.max(0, Math.floor((Date.now() - timestamp) / 1000))
        if (seconds < 60)
            return translations.i18n("Just now")
        const minutes = Math.floor(seconds / 60)
        if (minutes < 60)
            return translations.i18np("1 minute ago", "%1 minutes ago", minutes)
        const hours = Math.floor(minutes / 60)
        if (hours < 24)
            return translations.i18np("1 hour ago", "%1 hours ago", hours)
        const days = Math.floor(hours / 24)
        if (days < 30)
            return translations.i18np("1 day ago", "%1 days ago", days)
        const months = Math.floor(days / 30)
        if (months < 12)
            return translations.i18np("1 month ago", "%1 months ago", months)
        const years = Math.floor(days / 365)
        return translations.i18np("1 year ago", "%1 years ago", years)
    }

    function lastSuccessText(translations, value, revision) {
        const timestamp = Date.parse(value)
        if (isNaN(timestamp))
            return translations.i18n("No successful backup")
        const completed = new Date(timestamp)
        const now = new Date()
        if (completed.getFullYear() === now.getFullYear()
                && completed.getMonth() === now.getMonth()
                && completed.getDate() === now.getDate())
            return translations.i18n("today, %1", Qt.formatTime(completed, "HH:mm"))
        return relativeTime(translations, value, revision)
    }

    function historySummary(translations, entry) {
        const parts = [statusText(translations, entry.state)]
        if (entry.durationSeconds >= 0)
            parts.push(formatDuration(translations, entry.durationSeconds))
        if (entry.sourceCount > 0)
            parts.push(translations.i18np("1 source", "%1 sources", entry.sourceCount))
        if ((entry.errorCode?.length ?? 0) > 0)
            parts.push(entry.errorCode)
        return parts.join(" · ")
    }
}
