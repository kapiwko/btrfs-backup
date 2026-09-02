// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KLocalizedString>
#include <QDBusError>
#include <QString>

namespace btrfsbackup::kde::kcm {

inline QString manager_error_message(const QDBusError& error) {
    const QString name = error.name();
    if (name.endsWith(QStringLiteral(".InvalidRequest")))
        return i18nd("kcm_btrfsbackup", "The request is invalid.");
    if (name.endsWith(QStringLiteral(".SourceMissing")))
        return i18nd("kcm_btrfsbackup", "The selected backup source does not exist.");
    if (name.endsWith(QStringLiteral(".SourceNotSubvolume")))
        return i18nd("kcm_btrfsbackup", "The selected backup source is not a Btrfs subvolume.");
    if (name.endsWith(QStringLiteral(".SourceUnavailable")))
        return i18nd("kcm_btrfsbackup", "The selected backup source cannot be inspected.");
    if (name.endsWith(QStringLiteral(".NotFound")))
        return i18nd("kcm_btrfsbackup", "The requested item was not found.");
    if (name.endsWith(QStringLiteral(".NotAuthorized")))
        return i18nd("kcm_btrfsbackup", "The operation was cancelled or you do not have permission to perform it.");
    if (name.endsWith(QStringLiteral(".Busy")))
        return i18nd("kcm_btrfsbackup", "The requested item is busy.");
    if (name.endsWith(QStringLiteral(".RunMismatch")))
        return i18nd("kcm_btrfsbackup", "The active backup run has changed. Refresh the page and try again.");
    if (name.endsWith(QStringLiteral(".TargetUnavailable")))
        return i18nd("kcm_btrfsbackup", "The backup device is disconnected or unavailable.");
    if (name.endsWith(QStringLiteral(".Conflict")))
        return i18nd("kcm_btrfsbackup", "The operation conflicts with the current state. Rescan and try again.");
    if (name.endsWith(QStringLiteral(".SaveFailed")))
        return i18nd("kcm_btrfsbackup", "The configuration could not be saved.");
    if (name.endsWith(QStringLiteral(".RollbackIncomplete")))
        return i18nd("kcm_btrfsbackup", "The operation could not be fully rolled back. Review the system log.");
    return i18nd("kcm_btrfsbackup", "The backup manager could not complete the request.");
}

} // namespace btrfsbackup::kde::kcm
