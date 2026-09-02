// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

#include <backup/ports/IBtrfsOperations.hpp>
#include <core/Errors.hpp>
#include <daemon/control/ExistingTargetInspector.hpp>
#include <platform/linux/storage/BlockDeviceMetadata.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>
#include <platform/linux/storage/ExistingTargetMountOperations.hpp>
#include <support/TestHelpers.hpp>

namespace fs = std::filesystem;
namespace bb = btrfsbackup;

namespace {

class Cryptsetup final : public bb::platform::linux::storage::ICryptsetupOperations {
  public:
    std::vector<std::string>& calls;
    explicit Cryptsetup(std::vector<std::string>& calls) : calls(calls) {
    }
    bb::platform::linux::storage::LuksHeader inspect_luks2(const fs::path& device) override {
        calls.push_back("inspect:" + device.string());
        return {.uuid = "luks-uuid", .keyslots = {1}};
    }
    void add_key(const fs::path&, int, int) override {
    }
    void test_key(const fs::path&, int) override {
    }
    void remove_keyslot(const fs::path&, int, int) override {
    }
    fs::path active_device(const std::string&) override {
        return {};
    }
    std::string format_luks2(const fs::path&, int) override {
        return {};
    }
    void open_luks2(const fs::path&, const std::string&, int) override {
    }
    void open_luks2_read_only(const fs::path& device, const std::string& mapper, int) override {
        calls.push_back("open-ro:" + device.string() + ":" + mapper);
    }
    void close(const std::string& mapper) override {
        calls.push_back("close:" + mapper);
    }
};

class Metadata final : public bb::platform::linux::storage::IBlockDeviceMetadataReader {
  public:
    std::string filesystem_type = "btrfs";
    bb::platform::linux::storage::BlockDeviceMetadata read(const fs::path&) override {
        return {.filesystem_type = filesystem_type, .filesystem_uuid = "btrfs-uuid"};
    }
};

class Mounts final : public bb::platform::linux::storage::IExistingTargetMountOperations {
  public:
    std::vector<std::string>& calls;
    explicit Mounts(std::vector<std::string>& calls) : calls(calls) {
    }
    void mount_btrfs_read_only(const fs::path&, const fs::path&) override {
        calls.push_back("mount-ro");
    }
    void unmount(const fs::path&) override {
        calls.push_back("unmount");
    }
};

class Btrfs final : public bb::backup::IBtrfsOperations {
  public:
    bool is_subvolume(const fs::path&) override {
        return true;
    }
    std::optional<bb::backup::SnapshotMetadata> read_snapshot_metadata(const fs::path&) override {
        return bb::backup::SnapshotMetadata{
            .is_subvolume = true,
            .readonly = true,
            .uuid = bb::backup::SnapshotUuid("snapshot-uuid"),
            .received_uuid = bb::backup::ReceivedSnapshotUuid("received-uuid"),
        };
    }
    void create_readonly_snapshot(const fs::path&, const fs::path&) override {
    }
    void delete_subvolume(const fs::path&) override {
    }
};

bb::daemon::provisioning::ExistingPartition partition() {
    bb::daemon::provisioning::ExistingPartition result;
    result.identity.display_path = "/dev/test1";
    result.partition_uuid = "partition-uuid";
    result.filesystem.type = "crypto_LUKS";
    result.filesystem.uuid = "luks-uuid";
    result.suitable_for_adoption = true;
    return result;
}

void write_repository(const fs::path& root) {
    test_helpers::write_file(root / "repository.json", R"({
        "schemaVersion": 1,
        "repositoryId": "repository-1",
        "targetFilesystemUuid": "btrfs-uuid",
        "createdAt": "2026-09-02T120000Z",
        "features": ["catalog-v1"]
    })");
    test_helpers::write_file(root / "catalog.json", R"({
        "schemaVersion": 1,
        "generation": 4,
        "snapshots": []
    })");
}

void test_inspects_and_closes_read_only_session() {
    const fs::path root = test_helpers::test_root("existing-target-inspector", "success");
    write_repository(root);
    std::vector<std::string> calls;
    Cryptsetup cryptsetup(calls);
    Metadata metadata;
    Mounts mounts(calls);
    Btrfs btrfs;
    bb::daemon::control::ExistingTargetInspector inspector(cryptsetup, metadata, mounts, btrfs);

    const auto summary = inspector.inspect(partition(), "inspection-test", root, 8);
    test_helpers::expect_eq("inspected LUKS UUID", summary.luks_uuid, "luks-uuid");
    test_helpers::expect_eq("inspected Btrfs UUID", summary.btrfs_uuid, "btrfs-uuid");
    test_helpers::expect_eq("inspected repository", summary.repository_id, "repository-1");
    test_helpers::expect_eq("inspection cleanup order", calls.at(calls.size() - 2), "unmount");
    test_helpers::expect_eq("inspection closes mapper", calls.back(), "close:inspection-test");
}

void test_closes_mapper_when_filesystem_is_not_btrfs() {
    const fs::path root = test_helpers::test_root("existing-target-inspector", "not-btrfs");
    std::vector<std::string> calls;
    Cryptsetup cryptsetup(calls);
    Metadata metadata;
    metadata.filesystem_type = "ext4";
    Mounts mounts(calls);
    Btrfs btrfs;
    bb::daemon::control::ExistingTargetInspector inspector(cryptsetup, metadata, mounts, btrfs);
    const auto summary = inspector.inspect(partition(), "inspection-test", root, 8);
    test_helpers::expect_true(
        "non-Btrfs classification",
        summary.classification == bb::daemon::provisioning::ExistingTargetClassification::NotBtrfsFilesystem,
        "non-Btrfs target was not classified"
    );
    test_helpers::expect_eq("failed inspection closes mapper", calls.back(), "close:inspection-test");
    test_helpers::expect_true(
        "failed inspection does not mount",
        std::ranges::find(calls, "mount-ro") == calls.end(),
        "non-Btrfs target was mounted"
    );
}

void test_classifies_repositoryless_btrfs_filesystems() {
    const auto inspect = [](const fs::path& root) {
        std::vector<std::string> calls;
        Cryptsetup cryptsetup(calls);
        Metadata metadata;
        Mounts mounts(calls);
        Btrfs btrfs;
        bb::daemon::control::ExistingTargetInspector inspector(cryptsetup, metadata, mounts, btrfs);
        return inspector.inspect(partition(), "inspection-test", root, 8);
    };

    const fs::path empty = test_helpers::test_root("existing-target-inspector", "empty");
    const auto empty_summary = inspect(empty);
    test_helpers::expect_true(
        "empty Btrfs classification",
        empty_summary.classification == bb::daemon::provisioning::ExistingTargetClassification::EmptyFilesystem,
        "empty Btrfs target was not classified"
    );

    const fs::path legacy = test_helpers::test_root("existing-target-inspector", "legacy");
    fs::create_directories(legacy / "default/snapshots/home");
    const auto legacy_summary = inspect(legacy);
    test_helpers::expect_true(
        "legacy classification",
        legacy_summary.classification == bb::daemon::provisioning::ExistingTargetClassification::LegacyRepository,
        "legacy target was not recognized"
    );

    const fs::path foreign = test_helpers::test_root("existing-target-inspector", "foreign");
    test_helpers::write_file(foreign / "unrelated.txt", "data");
    const auto foreign_summary = inspect(foreign);
    test_helpers::expect_true(
        "foreign classification",
        foreign_summary.classification == bb::daemon::provisioning::ExistingTargetClassification::ForeignOrInvalidRepository,
        "foreign target was not rejected"
    );
}

void test_classifies_unsupported_repository_format() {
    const fs::path root = test_helpers::test_root("existing-target-inspector", "unsupported");
    test_helpers::write_file(root / "repository.json", R"({"schemaVersion": 99})");
    std::vector<std::string> calls;
    Cryptsetup cryptsetup(calls);
    Metadata metadata;
    Mounts mounts(calls);
    Btrfs btrfs;
    bb::daemon::control::ExistingTargetInspector inspector(cryptsetup, metadata, mounts, btrfs);
    const auto summary = inspector.inspect(partition(), "inspection-test", root, 8);
    test_helpers::expect_true(
        "unsupported repository classification",
        summary.classification == bb::daemon::provisioning::ExistingTargetClassification::UnsupportedRepository &&
            summary.diagnostic_code == "repository-format-unsupported",
        "unsupported repository format was not classified"
    );
}

} // namespace

int main() {
    test_inspects_and_closes_read_only_session();
    test_closes_mapper_when_filesystem_is_not_btrfs();
    test_classifies_repositoryless_btrfs_filesystems();
    test_classifies_unsupported_repository_format();
    return test_helpers::finish("existing target inspector tests");
}
