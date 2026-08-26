// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/model/backup_run_event.hpp>
#include <core/durable_file_operations.hpp>

namespace btrfsbackup {

class JsonFileBackupRunCheckpointStore final : public IBackupRunCheckpointStore {
  public:
    JsonFileBackupRunCheckpointStore(
        IDurableFileOperations& files,
        std::filesystem::path profile_state_dir
    );

    void write_checkpoint(const BackupRunCheckpoint& checkpoint) override;

  private:
    IDurableFileOperations& files_;
    std::filesystem::path profile_state_dir_;
};

} // namespace btrfsbackup
