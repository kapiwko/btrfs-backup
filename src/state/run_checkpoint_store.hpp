// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/backup_run_event.hpp>

namespace btrfsbackup {

class JsonFileBackupRunCheckpointStore final : public IBackupRunCheckpointStore {
  public:
    explicit JsonFileBackupRunCheckpointStore(std::filesystem::path profile_state_dir);

    void write_checkpoint(const BackupRunCheckpoint& checkpoint) override;

  private:
    std::filesystem::path profile_state_dir_;
};

} // namespace btrfsbackup
