// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <restore/RestoreOperations.hpp>
#include <restore/RestorePlan.hpp>

namespace btrfsbackup::restore {

struct RestoreResult {
    std::string transaction_id;
    RestoreStatistics statistics;
    bool drill = false;
    bool committed = false;
};

class RestoreExecutor {
  public:
    explicit RestoreExecutor(IRestoreOperations& operations);

    RestoreResult execute(
        const RestorePlan& plan,
        CancellationToken& cancellation,
        const RestoreProgressSink& progress = {},
        const RestorePhaseSink& phase = {}
    );

  private:
    IRestoreOperations& operations_;
};

} // namespace btrfsbackup::restore
