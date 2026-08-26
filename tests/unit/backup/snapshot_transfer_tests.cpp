// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <core/errors.hpp>
#include <backup/snapshot_transfer.hpp>

#include "support/validation_test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

class FakeBtrfsOperations final : public btrfsbackup::IBtrfsOperations {
public:
    std::optional<btrfsbackup::SnapshotMetadata> metadata;
    std::vector<std::string> calls;
    bool delete_throws = false;

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
        if (delete_throws) {
            throw btrfsbackup::ValidationError("injected delete failure");
        }
    }
};

class FakeFileSystem final : public btrfsbackup::IFileSystem {
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

    void remove_directory(const fs::path& path) override {
        calls.push_back("rmdir:" + path.string());
    }

    void remove_tree(const fs::path& path) override {
        calls.push_back("remove-tree:" + path.string());
    }

    void rename_path(const fs::path& source, const fs::path& target) override {
        calls.push_back("rename:" + source.string() + "->" + target.string());
    }

    std::vector<fs::path> list_directory(const fs::path& path) override {
        calls.push_back("list:" + path.string());
        return {};
    }
};

void test_builds_send_receive_commands() {
    btrfsbackup::SendReceiveCommandPlan full = btrfsbackup::build_send_receive_command_plan(
        "/snap/current",
        {},
        "/incoming/run"
    );
    test_helpers::expect_eq("full send argc", std::to_string(full.send_argv.size()), "6");
    test_helpers::expect_eq("full send protocol flag", full.send_argv.at(2), "--proto");
    test_helpers::expect_eq("full send protocol", full.send_argv.at(3), "2");
    test_helpers::expect_eq("full send compressed data", full.send_argv.at(4), "--compressed-data");
    test_helpers::expect_eq("full send path", full.send_argv.at(5), "/snap/current");
    test_helpers::expect_eq("receive dir", full.receive_argv.at(2), "/incoming/run");

    btrfsbackup::SendReceiveCommandPlan incremental = btrfsbackup::build_send_receive_command_plan(
        "/snap/current",
        "/snap/parent",
        "/incoming/run"
    );
    test_helpers::expect_eq("incremental send argc", std::to_string(incremental.send_argv.size()), "8");
    test_helpers::expect_eq("incremental protocol flag", incremental.send_argv.at(2), "--proto");
    test_helpers::expect_eq("incremental protocol", incremental.send_argv.at(3), "2");
    test_helpers::expect_eq("incremental compressed data", incremental.send_argv.at(4), "--compressed-data");
    test_helpers::expect_eq("incremental parent flag", incremental.send_argv.at(5), "-p");
    test_helpers::expect_eq("incremental parent path", incremental.send_argv.at(6), "/snap/parent");
    test_helpers::expect_eq("incremental send path", incremental.send_argv.at(7), "/snap/current");
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
    FakeFileSystem fs_effects;

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
    FakeFileSystem fs_effects;

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
    FakeFileSystem fs_effects;
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

void test_commit_reports_verification_and_cleanup_failure() {
    FakeBtrfsOperations btrfs;
    btrfs.metadata = btrfsbackup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .received_uuid = "wrong-uuid",
    };
    btrfs.delete_throws = true;
    FakeFileSystem fs_effects;

    try {
        btrfsbackup::commit_received_snapshot(
            btrfs,
            fs_effects,
            "/incoming/run/home",
            "/remote/home/home-2026-08-23T080000Z",
            "expected-uuid"
        );
        test_helpers::fail("commit cleanup failure", "expected RecoveryRequiredError");
    } catch (const btrfsbackup::RecoveryRequiredError& error) {
        test_helpers::expect_eq("commit cleanup error code", error.error_code, "repository.recovery_required");
        test_helpers::expect_contains("commit verification error", error.what(), "Committed snapshot Received UUID");
        test_helpers::expect_contains("commit cleanup error", error.what(), "cleanup failed");
        test_helpers::expect_contains("commit recovery state", error.what(), "repository requires recovery");
    } catch (const std::exception& error) {
        test_helpers::fail("commit cleanup failure", std::string("unexpected exception: ") + error.what());
    }
}

} // namespace

int main() {
    test_builds_send_receive_commands();
    test_verifies_received_snapshot();
    test_commit_received_snapshot();
    test_commit_deletes_invalid_final_snapshot();
    test_commit_rejects_existing_destination();
    test_commit_reports_verification_and_cleanup_failure();

    return test_helpers::finish("snapshot transfer tests");
}
