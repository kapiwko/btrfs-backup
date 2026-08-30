// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <platform/linux/LibBtrfsOperations.hpp>

#include "support/TestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

class FakeBtrfsOperations final : public btrfsbackup::backup::IBtrfsOperations {
  public:
    std::optional<btrfsbackup::backup::SnapshotMetadata> metadata;
    std::vector<std::string> calls;

    bool is_subvolume(const fs::path& path) override {
        calls.push_back("is:" + path.string());
        return metadata.has_value() && metadata->is_subvolume;
    }

    std::optional<btrfsbackup::backup::SnapshotMetadata> read_snapshot_metadata(const fs::path& path) override {
        calls.push_back("metadata:" + path.string());
        return metadata;
    }

    void create_readonly_snapshot(const fs::path& source, const fs::path& target) override {
        calls.push_back("snapshot:" + source.string() + "->" + target.string());
    }

    void delete_subvolume(const fs::path& path) override {
        calls.push_back("delete:" + path.string());
    }
};

void test_fake_operations_capture_expected_calls() {
    FakeBtrfsOperations ops;
    ops.metadata = btrfsbackup::backup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .uuid = "local-uuid",
        .received_uuid = "received-uuid",
    };

    test_helpers::expect_true("fake subvolume", ops.is_subvolume("/source"), "fake should report a subvolume");
    std::optional<btrfsbackup::backup::SnapshotMetadata> metadata = ops.read_snapshot_metadata("/snapshot");
    test_helpers::expect_true("fake metadata", metadata.has_value(), "fake metadata should exist");
    test_helpers::expect_eq("fake uuid", metadata->uuid, "local-uuid");

    ops.create_readonly_snapshot("/source", "/snapshot");
    ops.delete_subvolume("/snapshot");

    test_helpers::expect_eq("fake call count", std::to_string(ops.calls.size()), "4");
    test_helpers::expect_eq("fake snapshot call", ops.calls.at(2), "snapshot:/source->/snapshot");
    test_helpers::expect_eq("fake delete call", ops.calls.at(3), "delete:/snapshot");
}

void test_lib_operations_treat_regular_directory_as_not_subvolume() {
    fs::path root = test_helpers::test_root("btrfs-operations", "regular-dir");
    btrfsbackup::platform::linux::LibBtrfsOperations ops;

    test_helpers::expect_true(
        "regular dir not subvolume",
        !ops.is_subvolume(root),
        "regular temporary directory should not be a Btrfs subvolume"
    );
    std::optional<btrfsbackup::backup::SnapshotMetadata> metadata = ops.read_snapshot_metadata(root);
    test_helpers::expect_true("regular dir no metadata", !metadata.has_value(), "regular directory should not have Btrfs metadata");

    fs::remove_all(root);
}

} // namespace

int main() {
    test_fake_operations_capture_expected_calls();
    test_lib_operations_treat_regular_directory_as_not_subvolume();

    return test_helpers::finish("btrfs operations tests");
}
