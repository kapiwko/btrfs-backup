// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/run_checkpoint_store.hpp>

#include <sys/stat.h>

#include <utility>

#include <config/model/json_io.hpp>
#include <platform/linux/file_io.hpp>
#include <state/backup_run_serialization.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

JsonFileBackupRunCheckpointStore::JsonFileBackupRunCheckpointStore(fs::path profile_state_dir)
    : profile_state_dir_(std::move(profile_state_dir)) {
}

void JsonFileBackupRunCheckpointStore::write_checkpoint(const BackupRunCheckpoint& checkpoint) {
    fs::create_directories(profile_state_dir_);
    chmod(profile_state_dir_.c_str(), 0700);
    atomic_write(profile_state_dir_ / "checkpoint.json", dump_json(build_backup_run_checkpoint_json(checkpoint)), 0600);
}

} // namespace btrfsbackup
