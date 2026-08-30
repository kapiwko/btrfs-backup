// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/model/BackupRunCheckpoint.hpp>

namespace btrfsbackup::backup {

class IBackupRunCheckpointStore {
  public:
    virtual ~IBackupRunCheckpointStore() = default;
    virtual void write_checkpoint(const BackupRunCheckpoint& checkpoint) = 0;
};

} // namespace btrfsbackup::backup
