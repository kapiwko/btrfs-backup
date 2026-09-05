// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RestoreErrorPresentation.hpp"

#include <KLocalizedString>

#include <utility>

namespace btrfsbackup::kde::restore {

RestoreErrorPresentation present_restore_error(
    btrfsbackup::restore::RestoreErrorCode code,
    const QString& technical_details
) {
    QString message;
    using ErrorCode = btrfsbackup::restore::RestoreErrorCode;
    switch (code) {
    case ErrorCode::RepositoryNotFound:
        message = i18n("The backup repository could not be found.");
        break;
    case ErrorCode::RepositoryMetadataInvalid:
    case ErrorCode::CatalogInvalid:
        message = i18n("The selected backup repository could not be read.");
        break;
    case ErrorCode::RepositoryFormatUnsupported:
        message = i18n("This backup repository format is not supported.");
        break;
    case ErrorCode::SnapshotNotFound:
    case ErrorCode::SnapshotIdentityMismatch:
        message = i18n("The selected backup version is no longer available.");
        break;
    case ErrorCode::PathInvalid:
    case ErrorCode::PathTraversal:
    case ErrorCode::SymlinkRejected:
    case ErrorCode::MountBoundaryRejected:
        message = i18n("The selected backup path is not safe to restore.");
        break;
    case ErrorCode::DestinationExists:
        message = i18n("The restore destination already exists.");
        break;
    case ErrorCode::DestinationUnsafe:
        message = i18n("The selected restore destination is not safe.");
        break;
    case ErrorCode::InsufficientSpace:
        message = i18n("There is not enough free space to restore this backup.");
        break;
    case ErrorCode::Cancelled:
        message = i18n("The restore operation was cancelled.");
        break;
    case ErrorCode::CopyFailed:
        message = i18n("The backup data could not be copied to the destination.");
        break;
    case ErrorCode::VerificationFailed:
        message = i18n("The restored data could not be verified.");
        break;
    case ErrorCode::CleanupIncomplete:
        message = i18n("The restore failed and temporary files could not be fully removed.");
        break;
    case ErrorCode::RollbackIncomplete:
        message = i18n("The restore failed and the previous destination could not be fully recovered.");
        break;
    }
    return {
        std::move(message),
        QStringLiteral("restore.") + QString::fromStdString(btrfsbackup::restore::restore_error_code_name(code)),
        technical_details,
    };
}

RestoreErrorPresentation present_unexpected_restore_error(const QString& technical_details) {
    return {
        i18n("The restore operation could not be completed."),
        QStringLiteral("restore.unexpected"),
        technical_details,
    };
}

} // namespace btrfsbackup::kde::restore
