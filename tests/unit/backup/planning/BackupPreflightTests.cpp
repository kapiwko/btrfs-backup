// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <vector>

#include <backup/planning/BackupPreflight.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace {

btrfsbackup::config::Profile profile() {
    btrfsbackup::config::Profile result{
        btrfsbackup::ProfileId{"default"},
        {
            btrfsbackup::config::LuksUuid{"11111111-2222-3333-4444-555555555555"},
            btrfsbackup::config::BtrfsUuid{"22222222-3333-4444-5555-666666666666"},
            btrfsbackup::config::PartitionUuid{""},
            btrfsbackup::config::MapperName{"backup"},
        },
        {
            btrfsbackup::config::RemoteSnapshotRoot{"/mnt/backup/snapshots"},
            btrfsbackup::config::IncomingRoot{"/mnt/backup/.incoming"},
        },
    };
    result.target.mount_point = btrfsbackup::config::TargetMountPoint{"/mnt/backup"};
    return result;
}

struct FakeTargetManager final : btrfsbackup::backup::ITargetManager {
    explicit FakeTargetManager(std::vector<std::string>& calls) : calls(calls) {
    }

    bool mounted = false;
    std::vector<std::string>& calls;

    struct Session final : btrfsbackup::backup::IMountedTargetSession {
        Session(bool& mounted, std::vector<std::string>& calls, bool mounted_by_session)
            : mounted(mounted), calls(calls), mounted_by_session(mounted_by_session) {
        }
        ~Session() override {
            (void)close();
        }
        std::optional<btrfsbackup::backup::TargetCleanupError> close() noexcept override {
            if (!closed && mounted_by_session) {
                mounted = false;
                calls.push_back("unmount");
            }
            closed = true;
            return std::nullopt;
        }
        bool mounted_by_this_session() const noexcept override {
            return mounted_by_session;
        }
        bool& mounted;
        std::vector<std::string>& calls;
        bool mounted_by_session;
        bool closed = false;
    };

    std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> prepare(
        const btrfsbackup::config::Profile&,
        btrfsbackup::backup::TargetMountMode mode
    ) override {
        const bool should_mount = !mounted && mode == btrfsbackup::backup::TargetMountMode::MountIfNeeded;
        if (should_mount) {
            calls.push_back("mount");
            mounted = true;
        }
        return std::make_unique<Session>(mounted, calls, should_mount);
    }
};

struct FakeMountInspector final : btrfsbackup::backup::IMountInspector {
    FakeMountInspector(const bool& mounted, std::vector<std::string>& calls)
        : mounted(mounted), calls(calls) {
    }

    const bool& mounted;
    std::string filesystem_uuid = "22222222-3333-4444-5555-666666666666";
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
    btrfsbackup::backup::planning::BackupPreflight preflight(mounts, target);
    btrfsbackup::CancellationToken cancellation;

    std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> session = preflight.run(
        profile(),
        btrfsbackup::backup::TargetMountMode::MountIfNeeded,
        cancellation
    );

    test_helpers::expect_true(
        "preflight order",
        calls == std::vector<std::string>{"mount", "inspect"},
        "mount table was not read after target activation"
    );
    test_helpers::expect_true("preflight close", !session->close().has_value(), "target cleanup failed");
    test_helpers::expect_true(
        "preflight restores mount",
        calls == std::vector<std::string>{"mount", "inspect", "unmount"},
        "session did not restore the target mount state"
    );
}

void test_offline_preflight_does_not_activate_target() {
    std::vector<std::string> calls;
    FakeTargetManager target(calls);
    FakeMountInspector mounts(target.mounted, calls);
    btrfsbackup::backup::planning::BackupPreflight preflight(mounts, target);
    btrfsbackup::CancellationToken cancellation;

    test_helpers::expect_validation_error(
        "offline target",
        [&] { (void)preflight.run(profile(), btrfsbackup::backup::TargetMountMode::RequireMounted, cancellation); },
        "not mounted"
    );
    test_helpers::expect_true(
        "offline has no mount effects",
        calls == std::vector<std::string>{"inspect"},
        "offline preflight changed the target state"
    );
}

void test_rejects_identity_seen_after_target_activation() {
    std::vector<std::string> calls;
    FakeTargetManager target(calls);
    FakeMountInspector mounts(target.mounted, calls);
    mounts.filesystem_uuid = "replacement-fs";
    btrfsbackup::backup::planning::BackupPreflight preflight(mounts, target);
    btrfsbackup::CancellationToken cancellation;

    test_helpers::expect_validation_error(
        "post-activation identity",
        [&] { (void)preflight.run(profile(), btrfsbackup::backup::TargetMountMode::MountIfNeeded, cancellation); },
        "Btrfs UUID mismatch"
    );
    test_helpers::expect_true(
        "identity check order",
        calls == std::vector<std::string>{"mount", "inspect", "unmount"},
        "post-activation failure did not restore the target state"
    );
}

} // namespace

int main() {
    test_activates_target_before_reading_and_validating_mounts();
    test_offline_preflight_does_not_activate_target();
    test_rejects_identity_seen_after_target_activation();
    return test_helpers::finish("backup preflight tests");
}
