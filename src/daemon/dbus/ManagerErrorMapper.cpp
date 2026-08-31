// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerErrorMapper.hpp>

#include <core/Errors.hpp>

namespace btrfsbackup::daemon::dbus {

ManagerErrorDescription ManagerErrorMapper::map(const std::exception& error) const noexcept {
    if (const auto* manager_error = dynamic_cast<const ManagerOperationError*>(&error))
        return describe(manager_error->code());
    if (const auto* coded = dynamic_cast<const CodedError*>(&error)) {
        switch (coded->error_code) {
        case ErrorCode::RunnerProfileBusy:
        case ErrorCode::RunnerTargetBusy:
            return describe(ManagerErrorCode::Busy);
        case ErrorCode::RunnerStaleRun:
        case ErrorCode::RunnerRunMismatch:
            return describe(ManagerErrorCode::RunMismatch);
        case ErrorCode::TargetBtrfsUuidMismatch:
        case ErrorCode::RepositoryRecoveryRequired:
        case ErrorCode::ConfigurationChanged:
            return describe(ManagerErrorCode::Conflict);
        case ErrorCode::ConfigurationSaveFailed:
            return describe(ManagerErrorCode::SaveFailed);
        case ErrorCode::ConfigurationRollbackIncomplete:
            return describe(ManagerErrorCode::RollbackIncomplete);
        default:
            return describe(ManagerErrorCode::InternalError);
        }
    }
    if (dynamic_cast<const ValidationError*>(&error) != nullptr)
        return describe(ManagerErrorCode::InvalidRequest);
    return describe(ManagerErrorCode::InternalError);
}

ManagerErrorDescription ManagerErrorMapper::describe(ManagerErrorCode code) noexcept {
    switch (code) {
    case ManagerErrorCode::InvalidRequest:
        return {code, "io.github.btrfsbackup.Error.InvalidRequest", "manager request is invalid"};
    case ManagerErrorCode::SourceMissing:
        return {code, "io.github.btrfsbackup.Error.SourceMissing", "source subvolume does not exist"};
    case ManagerErrorCode::SourceNotSubvolume:
        return {code, "io.github.btrfsbackup.Error.SourceNotSubvolume", "source path is not a Btrfs subvolume"};
    case ManagerErrorCode::SourceUnavailable:
        return {code, "io.github.btrfsbackup.Error.SourceUnavailable", "source subvolume cannot be inspected"};
    case ManagerErrorCode::NotFound:
        return {code, "io.github.btrfsbackup.Error.NotFound", "requested resource was not found"};
    case ManagerErrorCode::NotAuthorized:
        return {code, "io.github.btrfsbackup.Error.NotAuthorized", "operation is not authorized"};
    case ManagerErrorCode::Busy:
        return {code, "io.github.btrfsbackup.Error.Busy", "requested resource is busy"};
    case ManagerErrorCode::RunMismatch:
        return {code, "io.github.btrfsbackup.Error.RunMismatch", "backup run does not match the request"};
    case ManagerErrorCode::TargetUnavailable:
        return {code, "io.github.btrfsbackup.Error.TargetUnavailable", "backup target is unavailable"};
    case ManagerErrorCode::Conflict:
        return {code, "io.github.btrfsbackup.Error.Conflict", "operation conflicts with the current state"};
    case ManagerErrorCode::SaveFailed:
        return {code, "io.github.btrfsbackup.Error.SaveFailed", "configuration could not be saved"};
    case ManagerErrorCode::RollbackIncomplete:
        return {code, "io.github.btrfsbackup.Error.RollbackIncomplete", "configuration rollback is incomplete"};
    case ManagerErrorCode::InternalError:
        return {code, "io.github.btrfsbackup.Error.InternalError", "manager request failed"};
    }
    return {ManagerErrorCode::InternalError, "io.github.btrfsbackup.Error.InternalError", "manager request failed"};
}

} // namespace btrfsbackup::daemon::dbus
