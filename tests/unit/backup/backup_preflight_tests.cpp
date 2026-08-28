// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <vector>

#include <backup/backup_preflight.hpp>

#include "support/validation_test_helpers.hpp"

namespace {

btrfsbackup::config::Profile profile() {
    btrfsbackup::config::Profile result{btrfsbackup::ProfileId{"default"}};
    result.target.mapper_name = "backup";
    result.target.mount_point = "/mnt/backup";
    result.target.btrfs_uuid = "target-fs";
    result.paths.remote_root = "/mnt/backup/snapshots";
    result.paths.incoming_root = "/mnt/backup/.incoming";
    return result;
}

struct FakeTargetManager final : btrfsbackup::backup::ITargetManager {
    explicit FakeTargetManager(std::vector<std::string>& calls) : calls(calls) {
    }

    bool mounted = false;
    std::vector<std::string>& calls;

    void ensure_mounted(const btrfsbackup::config::Profile&) override {
        calls.push_back("mount");
        mounted = true;
    }
};

struct FakeMountInspector final : btrfsbackup::backup::IMountInspector {
    FakeMountInspector(const bool& mounted, std::vector<std::string>& calls)
        : mounted(mounted), calls(calls) {
    }

    const bool& mounted;
    std::string filesystem_uuid = "target-fs";
    std::vector<std::string>& calls;

    std::vector<btrfsbackup::backup::MountEntry> inspect() const override {
        calls.push_back("inspect");
        if (!mounted) {
            return {};
        }
        return {{
            .source = "/dev/mapper/backup",
            .target = "/mnt/backup",
            .fstype = "btrfs",
            .options = "rw,nodev,nosuid,noexec,nosymfollow",
            .device_id = "0:21",
            .filesystem_uuid = filesystem_uuid,
        }};
    }
};

void test_activates_target_before_reading_and_validating_mounts() {
    std::vector<std::string> calls;
    FakeTargetManager target(calls);
    FakeMountInspector mounts(target.mounted, calls);
    btrfsbackup::backup::BackupPreflight preflight(mounts, target);

    preflight.run(profile());

    test_helpers::expect_true(
        "preflight order",
        calls == std::vector<std::string>{"mount", "inspect"},
        "mount table was not read after target activation"
    );
}

void test_rejects_identity_seen_after_target_activation() {
    std::vector<std::string> calls;
    FakeTargetManager target(calls);
    FakeMountInspector mounts(target.mounted, calls);
    mounts.filesystem_uuid = "replacement-fs";
    btrfsbackup::backup::BackupPreflight preflight(mounts, target);

    test_helpers::expect_validation_error(
        "post-activation identity",
        [&] { preflight.run(profile()); },
        "Btrfs UUID mismatch"
    );
    test_helpers::expect_true(
        "identity check order",
        calls == std::vector<std::string>{"mount", "inspect"},
        "post-activation mount identity was not inspected"
    );
}

} // namespace

int main() {
    test_activates_target_before_reading_and_validating_mounts();
    test_rejects_identity_seen_after_target_activation();
    return test_helpers::finish("backup preflight tests");
}
