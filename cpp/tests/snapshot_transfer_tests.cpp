#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <btrfsbackup/snapshot_transfer.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

class FakeBtrfsOperations final : public btrfsbackup::IBtrfsOperations {
public:
    std::optional<btrfsbackup::SnapshotMetadata> metadata;
    std::vector<std::string> calls;

    bool is_subvolume(const fs::path& path) override {
        calls.push_back("is:" + path.string());
        return metadata.has_value() && metadata->is_subvolume;
    }

    std::optional<btrfsbackup::SnapshotMetadata> read_snapshot_metadata(const fs::path& path) override {
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

class FakeFileSystemEffects final : public btrfsbackup::IFileSystemEffects {
public:
    bool final_exists = false;
    std::vector<std::string> calls;

    bool exists(const fs::path& path) override {
        calls.push_back("exists:" + path.string());
        return final_exists;
    }

    bool is_directory(const fs::path& path) override {
        calls.push_back("is-directory:" + path.string());
        return false;
    }

    void create_directories(const fs::path& path) override {
        calls.push_back("mkdir:" + path.string());
    }

    void remove_file(const fs::path& path) override {
        calls.push_back("remove:" + path.string());
    }

    void rename_path(const fs::path& source, const fs::path& target) override {
        calls.push_back("rename:" + source.string() + "->" + target.string());
    }
};

void test_builds_send_receive_commands() {
    btrfsbackup::SendReceiveCommandPlan full = btrfsbackup::build_send_receive_command_plan(
        "/snap/current",
        {},
        "/incoming/run"
    );
    test_helpers::expect_eq("full send argc", std::to_string(full.send_argv.size()), "3");
    test_helpers::expect_eq("full send path", full.send_argv.at(2), "/snap/current");
    test_helpers::expect_eq("receive dir", full.receive_argv.at(2), "/incoming/run");

    btrfsbackup::SendReceiveCommandPlan incremental = btrfsbackup::build_send_receive_command_plan(
        "/snap/current",
        "/snap/parent",
        "/incoming/run"
    );
    test_helpers::expect_eq("incremental send argc", std::to_string(incremental.send_argv.size()), "5");
    test_helpers::expect_eq("incremental parent flag", incremental.send_argv.at(2), "-p");
    test_helpers::expect_eq("incremental parent path", incremental.send_argv.at(3), "/snap/parent");
}

void test_verifies_received_snapshot() {
    btrfsbackup::SnapshotMetadata local{
        .is_subvolume = true,
        .readonly = true,
        .uuid = "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA",
    };
    btrfsbackup::SnapshotMetadata received{
        .is_subvolume = true,
        .readonly = true,
        .received_uuid = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
    };

    btrfsbackup::verify_received_snapshot("home", local, received);

    received.readonly = false;
    test_helpers::expect_validation_error("received readonly", [&] {
        btrfsbackup::verify_received_snapshot("home", local, received);
    }, "Received subvolume is not readonly");
}

void test_commit_received_snapshot() {
    FakeBtrfsOperations btrfs;
    btrfs.metadata = btrfsbackup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .received_uuid = "local-uuid",
    };
    FakeFileSystemEffects fs_effects;

    btrfsbackup::commit_received_snapshot(
        btrfs,
        fs_effects,
        "/incoming/run/home",
        "/remote/home/home-2026-08-23T080000Z",
        "LOCAL-UUID"
    );

    test_helpers::expect_eq("commit call count", std::to_string(btrfs.calls.size()), "2");
    test_helpers::expect_eq("commit snapshot call", btrfs.calls.at(0), "snapshot:/incoming/run/home->/remote/home/home-2026-08-23T080000Z");
    test_helpers::expect_eq("commit metadata call", btrfs.calls.at(1), "metadata:/remote/home/home-2026-08-23T080000Z");
}

void test_commit_deletes_invalid_final_snapshot() {
    FakeBtrfsOperations btrfs;
    btrfs.metadata = btrfsbackup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .received_uuid = "wrong-uuid",
    };
    FakeFileSystemEffects fs_effects;

    test_helpers::expect_validation_error("commit uuid mismatch", [&] {
        btrfsbackup::commit_received_snapshot(
            btrfs,
            fs_effects,
            "/incoming/run/home",
            "/remote/home/home-2026-08-23T080000Z",
            "expected-uuid"
        );
    }, "Committed snapshot Received UUID does not match the local snapshot UUID");
    test_helpers::expect_eq("commit cleanup call", btrfs.calls.back(), "delete:/remote/home/home-2026-08-23T080000Z");
}

void test_commit_rejects_existing_destination() {
    FakeBtrfsOperations btrfs;
    FakeFileSystemEffects fs_effects;
    fs_effects.final_exists = true;

    test_helpers::expect_validation_error("commit destination exists", [&] {
        btrfsbackup::commit_received_snapshot(
            btrfs,
            fs_effects,
            "/incoming/run/home",
            "/remote/home/home-2026-08-23T080000Z",
            "expected-uuid"
        );
    }, "Destination snapshot already exists");
}

} // namespace

int main() {
    test_builds_send_receive_commands();
    test_verifies_received_snapshot();
    test_commit_received_snapshot();
    test_commit_deletes_invalid_final_snapshot();
    test_commit_rejects_existing_destination();

    return test_helpers::finish("snapshot transfer tests");
}
