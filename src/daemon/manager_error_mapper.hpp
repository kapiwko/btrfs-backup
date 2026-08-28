// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <exception>

namespace btrfsbackup::daemon {

enum class ManagerErrorCode {
    InvalidRequest,
    NotFound,
    NotAuthorized,
    Busy,
    RunMismatch,
    TargetUnavailable,
    Conflict,
    InternalError,
};

struct ManagerErrorDescription {
    ManagerErrorCode code;
    const char* dbus_name;
    const char* public_message;
};

class ManagerErrorMapper {
  public:
    [[nodiscard]] ManagerErrorDescription map(const std::exception& error) const noexcept;
    [[nodiscard]] static ManagerErrorDescription describe(ManagerErrorCode code) noexcept;
};

} // namespace btrfsbackup::daemon
