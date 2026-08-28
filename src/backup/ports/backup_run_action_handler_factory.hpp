// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include <backup/model/backup_run_plan.hpp>

namespace btrfsbackup::backup {

class IBackupRunActionHandler;

class IBackupRunActionHandlerFactory {
  public:
    virtual ~IBackupRunActionHandlerFactory() = default;

    [[nodiscard]] virtual std::unique_ptr<IBackupRunActionHandler> create(
        const BackupRunPlan& plan
    ) = 0;
};

} // namespace btrfsbackup::backup
