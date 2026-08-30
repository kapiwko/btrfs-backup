// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/JsonFileBackupRunCheckpointStore.hpp>

#include <utility>

#include <config/model/JsonIo.hpp>
#include <state/BackupRunSerialization.hpp>

namespace fs = std::filesystem;

namespace {

constexpr fs::perms private_checkpoint_file_permissions =
    fs::perms::owner_read | fs::perms::owner_write;
constexpr fs::perms private_checkpoint_directory_permissions =
    private_checkpoint_file_permissions | fs::perms::owner_exec;

} // namespace

namespace btrfsbackup::state {

JsonFileBackupRunCheckpointStore::JsonFileBackupRunCheckpointStore(
    IAtomicDocumentWriter& files,
    fs::path profile_state_dir
)
    : files_(files), profile_state_dir_(std::move(profile_state_dir)) {
}

void JsonFileBackupRunCheckpointStore::write_checkpoint(const btrfsbackup::backup::BackupRunCheckpoint& checkpoint) {
    files_.ensure_directory(profile_state_dir_, private_checkpoint_directory_permissions);
    files_.write_atomically(
        profile_state_dir_ / "checkpoint.json",
        btrfsbackup::config::dump_json(build_backup_run_checkpoint_json(checkpoint)),
        private_checkpoint_file_permissions
    );
}

} // namespace btrfsbackup::state
