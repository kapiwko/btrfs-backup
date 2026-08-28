// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <sys/stat.h>

#include <config/model/json_io.hpp>
#include <state/run_checkpoint_store.hpp>
#include <platform/linux/posix_durable_file_operations.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

int mode_of(const fs::path& path) {
    struct stat info{};
    if (stat(path.c_str(), &info) != 0) {
        test_helpers::fail("stat", "cannot stat " + path.string());
        return 0;
    }
    return info.st_mode & 0777;
}

void test_checkpoint_store_writes_private_json_in_state_dir() {
    const fs::path root = test_helpers::test_root("run-checkpoint-store", "checkpoint");
    btrfsbackup::platform::linux::PosixDurableFileOperations durable_files;
    btrfsbackup::state::JsonFileBackupRunCheckpointStore store(durable_files, root / "state");

    store.write_checkpoint({
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"20260823T120000Z-123-456"},
        .source_id = btrfsbackup::SourceId{"root"},
        .action_kind = btrfsbackup::backup::BackupRunActionKind::CreateSnapshot,
    });

    const fs::path checkpoint = root / "state" / "checkpoint.json";
    const btrfsbackup::config::Json data = btrfsbackup::config::load_json_file(checkpoint);
    test_helpers::expect_true("checkpoint exists", fs::is_regular_file(checkpoint), "missing checkpoint");
    test_helpers::expect_true("checkpoint action", data.at("action") == "create-snapshot", "wrong action");
    test_helpers::expect_true("state dir mode", mode_of(root / "state") == 0700, "state dir should be private");
    test_helpers::expect_true("checkpoint mode", mode_of(checkpoint) == 0600, "checkpoint should be private");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_checkpoint_store_writes_private_json_in_state_dir();
    return test_helpers::finish("run checkpoint store tests");
}
