// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

namespace btrfsbackup::backup {

enum class BackupRunExecutionOutcome { Completed,
                                       Cancelled };

struct BackupRunExecutionResult {
    BackupRunExecutionOutcome outcome = BackupRunExecutionOutcome::Completed;
    std::size_t actions_completed = 0;
};

} // namespace btrfsbackup::backup
