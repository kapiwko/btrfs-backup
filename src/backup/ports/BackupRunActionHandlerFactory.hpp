// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include <backup/model/BackupRunPlan.hpp>

namespace btrfsbackup::backup::execution {
class IBackupRunActionHandler;
}

namespace btrfsbackup::backup {

class IBackupRunActionHandlerFactory {
  public:
    virtual ~IBackupRunActionHandlerFactory() = default;

    [[nodiscard]] virtual std::unique_ptr<execution::IBackupRunActionHandler> create(
        const BackupRunPlan& plan
    ) = 0;
};

} // namespace btrfsbackup::backup
