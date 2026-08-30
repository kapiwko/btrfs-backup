// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <variant>

#include <core/ErrorCode.hpp>

namespace btrfsbackup::backup {

struct BackupRunExecutionCompleted {
    std::size_t actions_completed = 0;
};

struct BackupRunExecutionCancelled {
    std::size_t actions_completed = 0;
};

struct BackupRunExecutionFailed {
    ErrorCode error_code;
    std::string error_message;
    std::size_t actions_completed = 0;
};

using BackupRunExecutionResult = std::variant<
    BackupRunExecutionCompleted,
    BackupRunExecutionCancelled,
    BackupRunExecutionFailed>;

} // namespace btrfsbackup::backup
