// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>

#include <platform/linux/storage/provisioning/ExistingTargetMountOperations.hpp>

#include "support/TestHelpers.hpp"
#include "support/ValidationTestHelpers.hpp"

namespace fs = std::filesystem;

int main() {
    btrfsbackup::platform::linux::storage::provisioning::LibmountExistingTargetMountOperations operations;
    test_helpers::expect_validation_error(
        "relative existing target source",
        [&] { operations.mount_btrfs_read_only("dev/mapper/backup", "/tmp"); },
        "source path is invalid"
    );
    test_helpers::expect_validation_error(
        "relative existing target mount point",
        [&] { operations.mount_btrfs_read_only("/dev/mapper/backup", "tmp/backup"); },
        "mount path is invalid"
    );

    const fs::path root = test_helpers::test_root("existing-target-mount", "validation");
    const fs::path regular_file = root / "mount-point";
    std::ofstream(regular_file).put('\n');
    test_helpers::expect_validation_error(
        "regular file existing target mount point",
        [&] { operations.mount_btrfs_read_only("/dev/mapper/backup", regular_file); },
        "directory is not trusted"
    );

    return test_helpers::finish("existing target mount operations tests");
}
