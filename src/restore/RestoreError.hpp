// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdexcept>
#include <string>

namespace btrfsbackup::restore {

enum class RestoreErrorCode {
    RepositoryNotFound,
    RepositoryMetadataInvalid,
    RepositoryFormatUnsupported,
    CatalogInvalid,
    SnapshotNotFound,
    SnapshotIdentityMismatch,
    PathInvalid,
    PathTraversal,
    SymlinkRejected,
    MountBoundaryRejected,
    DestinationExists,
    DestinationUnsafe,
    InsufficientSpace,
    Cancelled,
    CopyFailed,
    VerificationFailed,
    CleanupIncomplete,
    RollbackIncomplete,
};

[[nodiscard]] inline std::string restore_error_code_name(RestoreErrorCode code) {
    switch (code) {
    case RestoreErrorCode::RepositoryNotFound: return "repository-not-found";
    case RestoreErrorCode::RepositoryMetadataInvalid: return "repository-metadata-invalid";
    case RestoreErrorCode::RepositoryFormatUnsupported: return "repository-format-unsupported";
    case RestoreErrorCode::CatalogInvalid: return "catalog-invalid";
    case RestoreErrorCode::SnapshotNotFound: return "snapshot-not-found";
    case RestoreErrorCode::SnapshotIdentityMismatch: return "snapshot-identity-mismatch";
    case RestoreErrorCode::PathInvalid: return "path-invalid";
    case RestoreErrorCode::PathTraversal: return "path-traversal";
    case RestoreErrorCode::SymlinkRejected: return "symlink-rejected";
    case RestoreErrorCode::MountBoundaryRejected: return "mount-boundary-rejected";
    case RestoreErrorCode::DestinationExists: return "destination-exists";
    case RestoreErrorCode::DestinationUnsafe: return "destination-unsafe";
    case RestoreErrorCode::InsufficientSpace: return "insufficient-space";
    case RestoreErrorCode::Cancelled: return "cancelled";
    case RestoreErrorCode::CopyFailed: return "copy-failed";
    case RestoreErrorCode::VerificationFailed: return "verification-failed";
    case RestoreErrorCode::CleanupIncomplete: return "cleanup-incomplete";
    case RestoreErrorCode::RollbackIncomplete: return "rollback-incomplete";
    }
    return "restore-error";
}

class RestoreError final : public std::runtime_error {
  public:
    RestoreError(RestoreErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {
    }

    [[nodiscard]] RestoreErrorCode code() const noexcept {
        return code_;
    }

  private:
    RestoreErrorCode code_;
};

} // namespace btrfsbackup::restore
