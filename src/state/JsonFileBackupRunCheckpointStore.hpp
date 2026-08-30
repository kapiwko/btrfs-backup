// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/ports/IBackupRunCheckpointStore.hpp>
#include <state/PersistentDocumentOperations.hpp>

namespace btrfsbackup::state {

class JsonFileBackupRunCheckpointStore final : public btrfsbackup::backup::IBackupRunCheckpointStore {
  public:
    JsonFileBackupRunCheckpointStore(
        IAtomicDocumentWriter& files,
        std::filesystem::path profile_state_dir
    );

    void write_checkpoint(const btrfsbackup::backup::BackupRunCheckpoint& checkpoint) override;

  private:
    IAtomicDocumentWriter& files_;
    std::filesystem::path profile_state_dir_;
};

} // namespace btrfsbackup::state
