// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <string>
#include <vector>

#include <platform/linux/systemd_target_manager.hpp>

#include "support/test_helpers.hpp"

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
    result.target.mount_point = "/mnt/backup";
    return result;
}

struct FakeMountInspector final : btrfsbackup::backup::IMountInspector {
    bool mounted = false;

    std::vector<btrfsbackup::backup::MountEntry> inspect() const override {
        if (!mounted) {
            return {};
        }
        return {{.source = "/dev/mapper/backup", .target = "/mnt/backup"}};
    }
};

struct FakeCommandRunner final : btrfsbackup::backup::ICommandRunner {
    std::vector<std::vector<std::string>> calls;

    btrfsbackup::backup::CommandResult run(const std::vector<std::string>& argv) override {
        calls.push_back(argv);
        return {};
    }

    btrfsbackup::backup::CommandResult run_controlled(
        const std::vector<std::string>& argv,
        const btrfsbackup::backup::ControlledCommandOptions&
    ) override {
        return run(argv);
    }
};

void test_offline_session_does_not_mount_target() {
    FakeMountInspector mounts;
    FakeCommandRunner commands;
    btrfsbackup::platform::linux::SystemdTargetManager manager(mounts, commands);

    std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> session = manager.prepare(
        profile(),
        btrfsbackup::backup::TargetMountMode::RequireMounted
    );

    test_helpers::expect_true("offline session ownership", !session->mounted_by_this_session(), "offline session claimed the mount");
    test_helpers::expect_true("offline commands", commands.calls.empty(), "offline session invoked systemctl");
}

void test_mounted_session_restores_target_state() {
    FakeMountInspector mounts;
    FakeCommandRunner commands;
    btrfsbackup::platform::linux::SystemdTargetManager manager(mounts, commands);

    std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> session = manager.prepare(
        profile(),
        btrfsbackup::backup::TargetMountMode::MountIfNeeded
    );
    test_helpers::expect_true("mounted session ownership", session->mounted_by_this_session(), "session did not claim its mount");
    session.reset();

    const std::vector<std::vector<std::string>> expected{
        {"systemctl", "start", "mnt-backup.mount"},
        {"systemctl", "stop", "mnt-backup.mount"},
    };
    test_helpers::expect_true("mounted session commands", commands.calls == expected, "session did not restore the mount unit");
}

void test_existing_mount_is_not_stopped() {
    FakeMountInspector mounts;
    mounts.mounted = true;
    FakeCommandRunner commands;
    btrfsbackup::platform::linux::SystemdTargetManager manager(mounts, commands);

    std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> session = manager.prepare(
        profile(),
        btrfsbackup::backup::TargetMountMode::MountIfNeeded
    );
    session.reset();

    test_helpers::expect_true("existing mount commands", commands.calls.empty(), "session changed a pre-existing mount");
}

} // namespace

int main() {
    test_offline_session_does_not_mount_target();
    test_mounted_session_restores_target_state();
    test_existing_mount_is_not_stopped();
    return test_helpers::finish("systemd target manager tests");
}
