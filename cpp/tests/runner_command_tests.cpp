#include <sys/stat.h>

#include <filesystem>
#include <sstream>
#include <string>

#include <btrfsbackup/command/runner_command.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/profile.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::Profile test_profile(const fs::path& root) {
    btrfsbackup::Profile profile;
    profile.id = "default";
    profile.name = "Default backup";
    profile.enabled = true;
    profile.target.device = "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555";
    profile.target.luks_uuid = "11111111-2222-3333-4444-555555555555";
    profile.target.btrfs_uuid = "22222222-3333-4444-5555-666666666666";
    profile.target.partition_uuid = "";
    profile.target.serial = "";
    profile.target.mapper_name = "backup";
    profile.target.mount_point = (root / "target").string();
    profile.target.mount_unit = "target.mount";
    profile.paths.sources_dir = (root / "config" / "sources.d").string();
    profile.paths.remote_root = (root / "target" / "snapshots").string();
    profile.paths.incoming_root = (root / "target" / ".incoming").string();
    profile.paths.state_dir = (root / "state").string();
    profile.paths.status_root = (root / "status").string();
    profile.paths.history_root = (root / "history").string();
    profile.settings.incremental_required = false;
    profile.settings.keep_failed_local_snapshot = false;
    profile.settings.remote_retention = 2;
    profile.settings.local_retention = 2;
    profile.notifications.enabled = false;
    profile.notifications.method = "none";
    profile.sources = {
        {
            .id = "root",
            .name = "System",
            .enabled = true,
            .subvolume = (root / "source" / "root").string(),
            .local_snapshot_dir = (root / "source" / ".snapshots" / "root").string(),
            .remote_subdir = "root",
            .remote_retention = 2,
            .local_retention = 2,
        },
    };
    return profile;
}

void write_profile(const fs::path& config_root, const btrfsbackup::Profile& profile) {
    fs::path profile_path = config_root / "profiles" / profile.id / "profile.json";
    test_helpers::write_file(profile_path, btrfsbackup::profile_to_json(profile).dump(2));
    chmod(profile_path.c_str(), 0600);
}

void write_mountinfo(const fs::path& path, const btrfsbackup::Profile& profile) {
    test_helpers::write_file(
        path,
        "21 31 0:20 / " + fs::path(profile.sources.at(0).subvolume).string() + " rw,relatime - btrfs /dev/source rw\n"
        "22 31 0:20 / " + fs::path(profile.sources.at(0).local_snapshot_dir).string() + " rw,relatime - btrfs /dev/source rw\n"
        "23 31 0:21 / " + fs::path(profile.target.mount_point).string() + " rw,relatime - btrfs /dev/mapper/backup rw\n"
    );
}

void test_runner_plan_outputs_shadow_json() {
    fs::path root = test_helpers::test_root("runner-command", "plan");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    std::ostringstream output;
    int result = btrfsbackup::command::runner(
        config_root,
        {
            "plan",
            "--profile",
            "default",
            "--timestamp",
            "2026-08-23T080000Z",
            "--run-id",
            "20260823T080000Z-123-456",
            "--mountinfo",
            mountinfo.string(),
            "--mount-uuid",
            "/dev/source",
            "source-fs",
            "--mount-uuid",
            "/dev/mapper/backup",
            profile.target.btrfs_uuid,
        },
        output
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("runner result", std::to_string(result), "0");
    test_helpers::expect_eq("runner mode", json.at("mode").get<std::string>(), "shadow-plan");
    test_helpers::expect_eq("runner profile", json.at("profileId").get<std::string>(), "default");
    test_helpers::expect_eq("runner source count", std::to_string(json.at("sources").size()), "1");
    test_helpers::expect_eq(
        "runner planned snapshot",
        json.at("sources").at(0).at("localSnapshotPath").get<std::string>(),
        (root / "source" / ".snapshots" / "root" / "root-2026-08-23T080000Z").string()
    );
    test_helpers::expect_eq(
        "runner first action",
        json.at("sources").at(0).at("actions").at(0).at("kind").get<std::string>(),
        "cleanup-incoming"
    );

    fs::remove_all(root);
}

void test_runner_plan_validates_target_mount() {
    fs::path root = test_helpers::test_root("runner-command", "target-validation");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    std::ostringstream output;
    test_helpers::expect_validation_error("runner target uuid", [&] {
        (void)btrfsbackup::command::runner(
            config_root,
            {
                "plan",
                "--profile",
                "default",
                "--timestamp",
                "2026-08-23T080000Z",
                "--run-id",
                "20260823T080000Z-123-456",
                "--mountinfo",
                mountinfo.string(),
                "--mount-uuid",
                "/dev/source",
                "source-fs",
                "--mount-uuid",
                "/dev/mapper/backup",
                "99999999-9999-9999-9999-999999999999",
            },
            output
        );
    }, "Btrfs UUID mismatch");

    fs::remove_all(root);
}

void test_runner_execute_requires_experimental_guard_before_loading_profile() {
    fs::path root = test_helpers::test_root("runner-command", "execute-guard");

    std::ostringstream output;
    test_helpers::expect_validation_error("runner execute guard", [&] {
        (void)btrfsbackup::command::runner(
            root / "missing-config",
            {
                "execute",
                "--profile",
                "default",
            },
            output
        );
    }, "runner execute requires --experimental-cpp-runner");

    fs::remove_all(root);
}

} // namespace

int main() {
    test_runner_plan_outputs_shadow_json();
    test_runner_plan_validates_target_mount();
    test_runner_execute_requires_experimental_guard_before_loading_profile();

    return test_helpers::finish("runner command tests");
}
