// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdexcept>
#include <string>

namespace btrfsbackup::daemon::dbus {

enum class ManagerErrorCode {
    InvalidRequest,
    NotFound,
    NotAuthorized,
    Busy,
    RunMismatch,
    TargetUnavailable,
    Conflict,
    SaveFailed,
    RollbackIncomplete,
    InternalError,
};

class ManagerOperationError final : public std::runtime_error {
  public:
    ManagerOperationError(ManagerErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {
    }

    [[nodiscard]] ManagerErrorCode code() const noexcept {
        return code_;
    }

  private:
    ManagerErrorCode code_;
};

} // namespace btrfsbackup::daemon::dbus
