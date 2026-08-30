// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <platform/linux/systemd/SystemdTargetManager.hpp>

#include "support/TestHelpers.hpp"
#include "support/ValidationTestHelpers.hpp"

namespace {

namespace fs = std::filesystem;

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
    int start_exit_code = 0;
    int stop_mount_exit_code = 0;
    int stop_crypt_exit_code = 0;

    btrfsbackup::backup::CommandResult run(const std::vector<std::string>& argv) override {
        calls.push_back(argv);
        if (argv == std::vector<std::string>{"systemctl", "start", "mnt-backup.mount"}) {
            return {.exit_code = start_exit_code};
        }
        if (argv == std::vector<std::string>{"systemctl", "stop", "mnt-backup.mount"}) {
            return {.exit_code = stop_mount_exit_code};
        }
        if (argv == std::vector<std::string>{"systemctl", "stop", "btrfs-backup-target@default.service"}) {
            return {.exit_code = stop_crypt_exit_code};
        }
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
    btrfsbackup::platform::linux::systemd::SystemdTargetManager manager(mounts, commands);

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
    btrfsbackup::platform::linux::systemd::SystemdTargetManager manager(mounts, commands);

    std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> session = manager.prepare(
        profile(),
        btrfsbackup::backup::TargetMountMode::MountIfNeeded
    );
    test_helpers::expect_true("mounted session ownership", session->mounted_by_this_session(), "session did not claim its mount");
    test_helpers::expect_true("mounted session close", !session->close().has_value(), "session cleanup failed");

    const std::vector<std::vector<std::string>> expected{
        {"systemctl", "start", "mnt-backup.mount"},
        {"systemctl", "stop", "mnt-backup.mount"},
        {"systemctl", "stop", "btrfs-backup-target@default.service"},
    };
    test_helpers::expect_true("mounted session commands", commands.calls == expected, "session did not restore the target state");
}

void test_preexisting_mapper_is_not_stopped() {
    const fs::path mapper_root = test_helpers::test_root("systemd-target-manager", "active-mapper");
    fs::create_directories(mapper_root);
    test_helpers::write_file(mapper_root / "backup", "");
    FakeMountInspector mounts;
    FakeCommandRunner commands;
    btrfsbackup::platform::linux::systemd::SystemdTargetManager manager(mounts, commands, mapper_root);

    std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> session = manager.prepare(
        profile(),
        btrfsbackup::backup::TargetMountMode::MountIfNeeded
    );
    test_helpers::expect_true("preexisting mapper close", !session->close().has_value(), "session cleanup failed");

    const std::vector<std::vector<std::string>> expected{
        {"systemctl", "start", "mnt-backup.mount"},
        {"systemctl", "stop", "mnt-backup.mount"},
    };
    test_helpers::expect_true("existing mapper commands", commands.calls == expected, "session stopped a pre-existing mapper");
    fs::remove_all(mapper_root);
}

void test_existing_mount_is_not_stopped() {
    FakeMountInspector mounts;
    mounts.mounted = true;
    FakeCommandRunner commands;
    btrfsbackup::platform::linux::systemd::SystemdTargetManager manager(mounts, commands);

    std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> session = manager.prepare(
        profile(),
        btrfsbackup::backup::TargetMountMode::MountIfNeeded
    );
    test_helpers::expect_true("existing mount close", !session->close().has_value(), "session cleanup failed");

    test_helpers::expect_true("existing mount commands", commands.calls.empty(), "session changed a pre-existing mount");
}

void test_failed_mount_start_restores_inactive_mapper() {
    FakeMountInspector mounts;
    FakeCommandRunner commands;
    commands.start_exit_code = 1;
    btrfsbackup::platform::linux::systemd::SystemdTargetManager manager(mounts, commands);

    test_helpers::expect_validation_error(
        "failed mount start",
        [&] {
            (void)manager.prepare(profile(), btrfsbackup::backup::TargetMountMode::MountIfNeeded);
        },
        "could not start target mount unit"
    );
    const std::vector<std::vector<std::string>> expected{
        {"systemctl", "start", "mnt-backup.mount"},
        {"systemctl", "stop", "mnt-backup.mount"},
        {"systemctl", "stop", "btrfs-backup-target@default.service"},
    };
    test_helpers::expect_true("failed mount cleanup", commands.calls == expected, "failed mount left target state changed");
}

void test_failed_unmount_does_not_close_mapper() {
    FakeMountInspector mounts;
    FakeCommandRunner commands;
    commands.stop_mount_exit_code = 1;
    btrfsbackup::platform::linux::systemd::SystemdTargetManager manager(mounts, commands);

    std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> session = manager.prepare(
        profile(),
        btrfsbackup::backup::TargetMountMode::MountIfNeeded
    );
    const std::optional<btrfsbackup::backup::TargetCleanupError> error = session->close();

    test_helpers::expect_true(
        "failed unmount result",
        error.has_value() &&
            error->stage == btrfsbackup::backup::TargetCleanupStage::MountUnit &&
            error->unit == "mnt-backup.mount" &&
            error->exit_code == 1,
        "failed unmount was not reported"
    );

    const std::vector<std::vector<std::string>> expected{
        {"systemctl", "start", "mnt-backup.mount"},
        {"systemctl", "stop", "mnt-backup.mount"},
    };
    test_helpers::expect_true("failed unmount cleanup", commands.calls == expected, "session closed mapper while mount remained active");
}

void test_failed_cryptsetup_stop_is_reported_separately() {
    FakeMountInspector mounts;
    FakeCommandRunner commands;
    commands.stop_crypt_exit_code = 2;
    btrfsbackup::platform::linux::systemd::SystemdTargetManager manager(mounts, commands);

    std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> session = manager.prepare(
        profile(),
        btrfsbackup::backup::TargetMountMode::MountIfNeeded
    );
    const std::optional<btrfsbackup::backup::TargetCleanupError> error = session->close();

    test_helpers::expect_true(
        "failed cryptsetup result",
        error.has_value() &&
            error->stage == btrfsbackup::backup::TargetCleanupStage::CryptsetupUnit &&
            error->unit == "btrfs-backup-target@default.service" &&
            error->exit_code == 2,
        "failed cryptsetup stop was not reported"
    );
    const std::vector<std::vector<std::string>> expected{
        {"systemctl", "start", "mnt-backup.mount"},
        {"systemctl", "stop", "mnt-backup.mount"},
        {"systemctl", "stop", "btrfs-backup-target@default.service"},
    };
    test_helpers::expect_true("failed cryptsetup cleanup", commands.calls == expected, "cleanup commands were incomplete");
}

} // namespace

int main() {
    test_offline_session_does_not_mount_target();
    test_mounted_session_restores_target_state();
    test_preexisting_mapper_is_not_stopped();
    test_existing_mount_is_not_stopped();
    test_failed_mount_start_restores_inactive_mapper();
    test_failed_unmount_does_not_close_mapper();
    test_failed_cryptsetup_stop_is_reported_separately();
    return test_helpers::finish("systemd target manager tests");
}
