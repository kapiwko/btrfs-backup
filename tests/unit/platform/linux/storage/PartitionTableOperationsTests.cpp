// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/PartitionTableOperations.hpp>

#include "support/ValidationTestHelpers.hpp"

int main() {
    btrfsbackup::platform::linux::storage::LibfdiskPartitionTableOperations operations;
    test_helpers::expect_validation_error(
        "relative partition table path",
        [&] { operations.replace_with_single_gpt_partition("dev/test", "8:0"); },
        "path is invalid"
    );
    test_helpers::expect_validation_error(
        "invalid partition table identity",
        [&] { operations.replace_with_single_gpt_partition("/dev/null", "invalid"); },
        "identity is invalid"
    );
    test_helpers::expect_validation_error(
        "non-block partition table target",
        [&] { operations.replace_with_single_gpt_partition("/dev/null", "1:3"); },
        "not a block device"
    );
    return test_helpers::finish("partition table operations tests");
}
