// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/provisioning/PartitionTableOperations.hpp>

#include "support/ValidationTestHelpers.hpp"

int main() {
    btrfsbackup::platform::linux::storage::provisioning::LibfdiskPartitionTableOperations operations;
    test_helpers::expect_validation_error(
        "relative partition table snapshot path",
        [&] {
            static_cast<void>(operations.snapshot_partition_table(
                "dev/test",
                "8:0",
                btrfsbackup::platform::linux::storage::provisioning::PartitionTableFormat::Gpt,
                "gpt-id",
                512
            ));
        },
        "path is invalid"
    );
    test_helpers::expect_validation_error(
        "non-block partition table snapshot target",
        [&] {
            static_cast<void>(operations.snapshot_partition_table(
                "/dev/null",
                "1:3",
                btrfsbackup::platform::linux::storage::provisioning::PartitionTableFormat::Gpt,
                "gpt-id",
                512
            ));
        },
        "not a block device"
    );
    test_helpers::expect_validation_error(
        "relative partition creation inspection path",
        [&] {
            static_cast<void>(operations.inspect_partition_creation(
                "dev/test",
                "8:0",
                "gpt-id",
                512,
                2048,
                4096,
                {.start_sector = 2048, .sector_count = 4096, .partition_number = 1}
            ));
        },
        "path is invalid"
    );
    test_helpers::expect_validation_error(
        "relative free-space planning path",
        [&] {
            static_cast<void>(operations.plan_partition_in_free_space(
                "dev/test",
                "8:0",
                "gpt-id",
                512,
                2048,
                4096
            ));
        },
        "path is invalid"
    );
    test_helpers::expect_validation_error(
        "relative whole-device planning path",
        [&] { static_cast<void>(operations.plan_single_gpt_partition("dev/test", "8:0", 512)); },
        "path is invalid"
    );
    test_helpers::expect_validation_error(
        "non-block whole-device planning target",
        [&] { static_cast<void>(operations.plan_single_gpt_partition("/dev/null", "1:3", 512)); },
        "not a block device"
    );
    test_helpers::expect_validation_error(
        "non-block free-space planning target",
        [&] {
            static_cast<void>(operations.plan_partition_in_free_space(
                "/dev/null",
                "1:3",
                "gpt-id",
                512,
                2048,
                4096
            ));
        },
        "not a block device"
    );
    test_helpers::expect_validation_error(
        "out-of-range free-space creation geometry",
        [&] {
            static_cast<void>(operations.create_partition_in_free_space(
                "/dev/null",
                "1:3",
                "gpt-id",
                512,
                2048,
                4096,
                {.start_sector = 1024, .sector_count = 4096, .partition_number = 1}
            ));
        },
        "planned partition geometry"
    );
    test_helpers::expect_validation_error(
        "relative partition table path",
        [&] {
            static_cast<void>(operations.replace_with_single_gpt_partition(
                "dev/test",
                "8:0",
                {.start_sector = 2048, .sector_count = 4096, .partition_number = 1}
            ));
        },
        "path is invalid"
    );
    test_helpers::expect_validation_error(
        "relative whole-device partition inspection path",
        [&] {
            static_cast<void>(operations.inspect_single_gpt_partition(
                "dev/test",
                "8:0",
                512,
                {.start_sector = 2048, .sector_count = 4096, .partition_number = 1}
            ));
        },
        "path is invalid"
    );
    test_helpers::expect_validation_error(
        "invalid partition table identity",
        [&] {
            static_cast<void>(operations.replace_with_single_gpt_partition(
                "/dev/null",
                "invalid",
                {.start_sector = 2048, .sector_count = 4096, .partition_number = 1}
            ));
        },
        "identity is invalid"
    );
    test_helpers::expect_validation_error(
        "non-block partition table target",
        [&] {
            static_cast<void>(operations.replace_with_single_gpt_partition(
                "/dev/null",
                "1:3",
                {.start_sector = 2048, .sector_count = 4096, .partition_number = 1}
            ));
        },
        "not a block device"
    );
    return test_helpers::finish("partition table operations tests");
}
