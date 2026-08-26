// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/run_checkpoint_store.hpp>

#include <utility>

#include <config/model/json_io.hpp>
#include <state/serialization.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

JsonFileBackupRunCheckpointStore::JsonFileBackupRunCheckpointStore(
    IDurableFileOperations& files,
    fs::path profile_state_dir
)
    : files_(files), profile_state_dir_(std::move(profile_state_dir)) {
}

void JsonFileBackupRunCheckpointStore::write_checkpoint(const BackupRunCheckpoint& checkpoint) {
    files_.ensure_directory(profile_state_dir_, private_directory_permissions);
    files_.write_atomically(
        profile_state_dir_ / "checkpoint.json",
        dump_json(build_backup_run_checkpoint_json(checkpoint)),
        private_file_permissions
    );
}

} // namespace btrfsbackup
