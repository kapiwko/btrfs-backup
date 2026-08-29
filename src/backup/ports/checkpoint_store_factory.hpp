// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include <backup/ports/backup_run_checkpoint_store.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup::backup {

class ICheckpointStoreFactory {
  public:
    virtual ~ICheckpointStoreFactory() = default;

    [[nodiscard]] virtual std::unique_ptr<IBackupRunCheckpointStore> checkpoints(
        const ProfileId& profile_id
    ) = 0;
};

} // namespace btrfsbackup::backup
