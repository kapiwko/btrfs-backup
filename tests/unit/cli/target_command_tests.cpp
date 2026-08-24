#include <sys/stat.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <cli/target_command.hpp>
#include <platform/linux/file_lock.hpp>
#include <config/json.hpp>
#include <config/json_io.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* luks_uuid = "11111111-2222-3333-4444-555555555555";
constexpr const char* btrfs_uuid = "22222222-3333-4444-5555-666666666666";
constexpr const char* mapper_name = "btrfsbackup-test-target-command";
constexpr const char* crypt_unit_name = "systemd-cryptsetup@btrfsbackup\\x2dtest\\x2dtarget\\x2dcommand.service";

class RecordingCommandRunner final : public btrfsbackup::ICommandRunner {
public:
    bool mounted = false;
    std::vector<std::string> calls;

    btrfsbackup::CommandResult run(const std::vector<std::string>& argv) override {
        calls.push_back(join(argv));
        if (argv == std::vector<std::string>{"cryptsetup", "luksUUID", "/dev/disk/by-uuid/target-luks"}) {
            return {0, std::string(luks_uuid) + "\n"};
        }
        if (argv == std::vector<std::string>{"systemd-escape", "--template=systemd-cryptsetup@.service", mapper_name}) {
            return {0, std::string(crypt_unit_name) + "\n"};
        }
        if (argv.size() == 3 && argv.at(0) == "systemctl" && argv.at(1) == "start") {
            mounted = true;
            return {};
        }
        if (argv.size() == 3 && argv.at(0) == "umount" && argv.at(1) == "--") {
            mounted = false;
            return {};
        }
        return {};
    }

    btrfsbackup::CommandResult run_controlled(
        const std::vector<std::string>& argv,
        const btrfsbackup::ControlledCommandOptions&
    ) override {
        return run(argv);
    }

private:
    static std::string join(const std::vector<std::string>& argv) {
        std::string out;
        for (const std::string& arg : argv) {
            if (!out.empty()) {
                out += ' ';
            }
            out += arg;
        }
        return out;
    }
};

btrfsbackup::Json profile_json(const std::string& mount_point, bool auto_eject = true) {
    return {
        {"schemaVersion", 1},
        {"profileId", "default"},
        {"name", "Default backup"},
        {"enabled", true},
        {"target", {
            {"device", "/dev/disk/by-uuid/target-luks"},
            {"luksUuid", luks_uuid},
            {"btrfsUuid", btrfs_uuid},
            {"mapperName", mapper_name}
        }},
        {"paths", {
            {"remoteRoot", mount_point + "/snapshots"},
            {"incomingRoot", mount_point + "/.incoming"}
        }},
        {"settings", {
            {"autoEject", auto_eject},
            {"remoteRetention", 2},
            {"localRetention", 2}
        }},
        {"sources", btrfsbackup::Json::array({
            {
                {"id", "home"},
                {"name", "home"},
                {"enabled", true},
                {"subvolume", "/home"},
                {"localSnapshotDir", "/.snapshots/home"},
                {"remoteSubdir", "home"},
                {"remoteRetention", 2},
                {"localRetention", 2}
            }
        })}
    };
}

fs::path write_profile(const fs::path& root, const std::string& mount_point, bool auto_eject = true) {
    test_helpers::write_file(
        root / "btrfs-backup.conf",
        "CONFIG_VERSION=1\nTARGET_MOUNT_ROOT=" + fs::path(mount_point).parent_path().string() + "\n"
    );
    chmod((root / "btrfs-backup.conf").c_str(), 0600);
    fs::path profile_path = root / "profiles" / "default" / "profile.json";
    test_helpers::write_file(profile_path, btrfsbackup::dump_json(profile_json(mount_point, auto_eject)));
    chmod(profile_path.c_str(), 0600);
    return profile_path;
}

std::vector<btrfsbackup::MountEntry> mounts_for(bool mounted, const std::string& mount_point) {
    if (!mounted) {
        return {};
    }
    return {
        {
            .source = std::string("/dev/mapper/") + mapper_name,
            .target = mount_point,
            .fstype = "btrfs",
            .root = "/",
            .options = "rw,noatime,nodev,nosuid,noexec,nosymfollow",
            .device_id = "253:9",
            .filesystem_uuid = btrfs_uuid,
        }
    };
}

bool contains_call(const RecordingCommandRunner& commands, const std::string& call) {
    for (const std::string& recorded : commands.calls) {
        if (recorded == call) {
            return true;
        }
    }
    return false;
}

bool contains_call_prefix(const RecordingCommandRunner& commands, const std::string& prefix) {
    for (const std::string& recorded : commands.calls) {
        if (recorded.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

void test_mount_starts_unit_and_validates_target() {
    fs::path root = test_helpers::test_root("target-command", "mount");
    std::string mount_point = (root / "mnt" / "default").string();
    write_profile(root, mount_point);
    RecordingCommandRunner commands;
    btrfsbackup::command::TargetExecutionServices services{
        commands,
        [&commands, mount_point] { return mounts_for(commands.mounted, mount_point); },
        root / "locks",
        root
    };
    std::ostringstream output;

    setenv("BTRFS_BACKUP_ALLOW_ROOTLESS_TESTS", "true", 1);
    int result = btrfsbackup::command::target(root, {"mount", "--profile", "default"}, output, &services);

    test_helpers::expect_eq("target mount result", std::to_string(result), "0");
    test_helpers::expect_true(
        "target mount starts systemd unit",
        contains_call_prefix(commands, "systemctl start "),
        "mount unit was not started"
    );
    test_helpers::expect_contains("target mount output", output.str(), "Backup target is mounted at " + mount_point + ".");
    fs::remove_all(root);
}

void test_eject_unmounts_and_stops_crypt_unit() {
    fs::path root = test_helpers::test_root("target-command", "eject");
    std::string mount_point = (root / "mnt" / "default").string();
    write_profile(root, mount_point);
    RecordingCommandRunner commands;
    commands.mounted = true;
    btrfsbackup::command::TargetExecutionServices services{
        commands,
        [&commands, mount_point] { return mounts_for(commands.mounted, mount_point); },
        root / "locks",
        root
    };
    std::ostringstream output;

    setenv("BTRFS_BACKUP_ALLOW_ROOTLESS_TESTS", "true", 1);
    int result = btrfsbackup::command::target(root, {"eject", "--profile", "default"}, output, &services);

    test_helpers::expect_eq("target eject result", std::to_string(result), "0");
    test_helpers::expect_true("target eject sync", contains_call(commands, "sync"), "sync was not called");
    test_helpers::expect_true("target eject unmount", contains_call(commands, "umount -- " + mount_point), "target was not unmounted");
    test_helpers::expect_true(
        "target eject stop crypt unit",
        contains_call(commands, std::string("systemctl stop ") + crypt_unit_name),
        "cryptsetup unit was not stopped"
    );
    fs::remove_all(root);
}

void test_internal_eject_honors_auto_eject_setting() {
    fs::path root = test_helpers::test_root("target-command", "auto-eject-disabled");
    std::string mount_point = (root / "mnt" / "default").string();
    write_profile(root, mount_point, false);
    RecordingCommandRunner commands;
    commands.mounted = true;
    btrfsbackup::command::TargetExecutionServices services{
        commands,
        [&commands, mount_point] { return mounts_for(commands.mounted, mount_point); },
        root / "locks",
        root
    };
    std::ostringstream output;

    setenv("BTRFS_BACKUP_ALLOW_ROOTLESS_TESTS", "true", 1);
    int result = btrfsbackup::command::target(root, {"eject", "--from-runner", "--profile", "default"}, output, &services);

    test_helpers::expect_eq("target eject auto disabled result", std::to_string(result), "0");
    test_helpers::expect_true("target eject auto disabled calls", commands.calls.empty(), "commands should not run");
    test_helpers::expect_contains("target eject auto disabled output", output.str(), "Automatic eject is disabled");
    fs::remove_all(root);
}

void test_eject_refuses_busy_target_without_running_commands() {
    fs::path root = test_helpers::test_root("target-command", "eject-busy");
    std::string mount_point = (root / "mnt" / "default").string();
    write_profile(root, mount_point);
    RecordingCommandRunner commands;
    commands.mounted = true;
    fs::path lock_root = root / "locks";
    btrfsbackup::command::TargetExecutionServices services{
        commands,
        [&commands, mount_point] { return mounts_for(commands.mounted, mount_point); },
        lock_root,
        root
    };
    btrfsbackup::FileLock active_target_lock(btrfsbackup::target_lock_path(lock_root, luks_uuid));
    test_helpers::expect_true(
        "target busy lock acquired",
        active_target_lock.try_acquire(),
        "test setup should acquire target lock"
    );
    std::ostringstream output;

    setenv("BTRFS_BACKUP_ALLOW_ROOTLESS_TESTS", "true", 1);
    int result = btrfsbackup::command::target(root, {"eject", "--profile", "default"}, output, &services);

    test_helpers::expect_eq("target busy result", std::to_string(result), "1");
    test_helpers::expect_true("target busy commands", commands.calls.empty(), "busy eject must not run commands");
    test_helpers::expect_contains("target busy output", output.str(), "Backup target is busy");
    active_target_lock.release();
    fs::remove_all(root);
}

void test_mount_rejects_symlinked_mount_point_without_chmod() {
    fs::path root = test_helpers::test_root("target-command", "mount-symlink");
    chmod(root.c_str(), 0755);
    fs::path mount_root = root / "mnt";
    fs::path mount_point = mount_root / "default";
    fs::path victim = root / "victim";
    fs::create_directories(mount_root);
    fs::create_directories(victim);
    chmod(victim.c_str(), 0700);
    fs::create_directory_symlink(victim, mount_point);
    write_profile(root, mount_point.string());

    RecordingCommandRunner commands;
    btrfsbackup::command::TargetExecutionServices services{
        commands,
        [&commands, mount_point] { return mounts_for(commands.mounted, mount_point.string()); },
        root / "locks",
        root
    };
    std::ostringstream output;

    setenv("BTRFS_BACKUP_ALLOW_ROOTLESS_TESTS", "true", 1);
    test_helpers::expect_validation_error("symlinked target mount rejected", [&] {
        (void)btrfsbackup::command::target(root, {"mount", "--profile", "default"}, output, &services);
    }, "without symlinks");

    struct stat victim_status {};
    stat(victim.c_str(), &victim_status);
    test_helpers::expect_true("symlink victim mode unchanged", (victim_status.st_mode & 0777) == 0700, "mount changed victim permissions");
    test_helpers::expect_true(
        "mount unit not started for symlink",
        !contains_call_prefix(commands, "systemctl start "),
        "mount unit started for an untrusted path"
    );
    fs::remove_all(root);
}

} // namespace

int main() {
    test_mount_starts_unit_and_validates_target();
    test_eject_unmounts_and_stops_crypt_unit();
    test_internal_eject_honors_auto_eject_setting();
    test_eject_refuses_busy_target_without_running_commands();
    test_mount_rejects_symlinked_mount_point_without_chmod();
    return test_helpers::finish("target command tests passed");
}
