// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <cli/runner_command.hpp>
#include <cli/backup_tool.hpp>
#include <backup/default_backup_planner.hpp>
#include <backup/default_backup_run_factory.hpp>
#include <backup/file_run_state_repository.hpp>
#include <config/profile_fingerprint.hpp>
#include <platform/linux/file_lock.hpp>
#include <platform/linux/file_backup_run_lease_provider.hpp>
#include <platform/linux/posix_command_runner.hpp>
#include <platform/linux/systemd_target_mounter.hpp>
#include <platform/linux/mount_info.hpp>
#include <config/model/json.hpp>
#include <config/model/json_io.hpp>
#include <config/model/profile.hpp>
#include <config/profile_repository.hpp>
#include <state/run_state.hpp>

#include "support/fake_safe_directory.hpp"
#include "support/validation_test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

std::string action_name(btrfsbackup::BackupRunActionKind kind) {
    return std::to_string(static_cast<int>(kind));
}

class RecordingActionHandler final : public btrfsbackup::IBackupRunActionHandler {
  public:
    std::vector<std::string> calls;
    std::vector<std::string> local_retention_deletes;
    std::vector<std::string> remote_retention_deletes;
    std::vector<std::string> recovered_pending_paths;
    btrfsbackup::BackupRunActionKind throw_on = btrfsbackup::BackupRunActionKind::SelectParent;
    bool should_throw = false;
    bool write_pending_on_snapshot = false;
    fs::path pending_state_dir;
    std::string pending_timestamp = "2026-08-23T08:00:00Z";

    void handle(
        const btrfsbackup::BackupRunAction& action,
        const btrfsbackup::BackupRunPlan& plan,
        btrfsbackup::CancellationToken&
    ) override {
        const btrfsbackup::BackupRunActionKind kind = btrfsbackup::backup_run_action_kind(action);
        calls.push_back(
            std::string(btrfsbackup::backup_run_action_source_id(action).value()) + ":" + action_name(kind)
        );
        if (const auto* retention = std::get_if<btrfsbackup::ApplyLocalRetentionAction>(&action)) {
            for (const btrfsbackup::SnapshotInfo& snapshot : retention->plan.delete_snapshots) {
                local_retention_deletes.push_back(snapshot.path.string());
            }
        }
        if (const auto* retention = std::get_if<btrfsbackup::ApplyRemoteRetentionAction>(&action)) {
            for (const btrfsbackup::SnapshotInfo& snapshot : retention->plan.delete_snapshots) {
                remote_retention_deletes.push_back(snapshot.path.string());
            }
        }
        if (const auto* recovery = std::get_if<btrfsbackup::RecoverPendingAction>(&action);
            recovery != nullptr && recovery->recovery.delete_local_snapshot) {
            recovered_pending_paths.push_back(recovery->recovery.local_snapshot_path.string());
        }
        if (const auto* snapshot = std::get_if<btrfsbackup::CreateSnapshotAction>(&action);
            write_pending_on_snapshot && snapshot != nullptr) {
            btrfsbackup::write_pending_marker(
                pending_state_dir,
                {
                    .source_name = std::string(snapshot->source_id.value()),
                    .local_snapshot_path = snapshot->snapshot.string(),
                    .final_snapshot_path = snapshot->final_remote_snapshot.string(),
                    .run_id = std::string(plan.run_id.value()),
                    .timestamp = pending_timestamp,
                }
            );
        }
        if (should_throw && kind == throw_on) {
            throw btrfsbackup::ValidationError("injected action failure: " + action_name(kind));
        }
    }
};

class ConfigurableTransferPipeline final : public btrfsbackup::ITransferPipeline {
  public:
    std::vector<btrfsbackup::TransferPipelinePlan> plans;
    btrfsbackup::TransferResult next_result{
        .producer = {
            .started = true,
            .exit_code = 0,
        },
        .consumer = {
            .started = true,
            .exit_code = 0,
        },
        .bytes_transferred = 1024,
    };
    std::optional<fs::path> request_cancel_path;

    btrfsbackup::TransferResult run(
        const btrfsbackup::TransferPipelinePlan& plan,
        btrfsbackup::ITransferEventSink& events,
        btrfsbackup::CancellationToken& cancellation
    ) override {
        plans.push_back(plan);
        events.on_transfer_event({
            .kind = btrfsbackup::TransferEventKind::Progress,
            .bytes_transferred = 1024,
            .bytes_produced = 1024,
            .delta_bytes = 1024,
            .elapsed_ms = 1000,
            .speed_bps = 1024,
        });
        if (request_cancel_path.has_value()) {
            btrfsbackup::write_cancel_request(*request_cancel_path);
            for (int i = 0; i < 20 && !cancellation.cancellation_requested(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            next_result.cancelled = cancellation.cancellation_requested();
        }
        return next_result;
    }
};

class BlockingTransferPipeline final : public btrfsbackup::ITransferPipeline {
  public:
    std::atomic_bool entered = false;
    std::atomic_bool allow_finish = false;

    btrfsbackup::TransferResult run(
        const btrfsbackup::TransferPipelinePlan&,
        btrfsbackup::ITransferEventSink&,
        btrfsbackup::CancellationToken&
    ) override {
        entered.store(true);
        while (!allow_finish.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return {
            .producer = {
                .started = true,
                .exit_code = 0,
            },
            .consumer = {
                .started = true,
                .exit_code = 0,
            },
            .bytes_transferred = 1024,
        };
    }
};

class CancellationAwareTransferPipeline final : public btrfsbackup::ITransferPipeline {
  public:
    std::atomic_bool entered = false;

    btrfsbackup::TransferResult run(
        const btrfsbackup::TransferPipelinePlan&,
        btrfsbackup::ITransferEventSink&,
        btrfsbackup::CancellationToken& cancellation
    ) override {
        entered.store(true);
        while (!cancellation.cancellation_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return {
            .producer = {.started = true, .exit_code = 143},
            .consumer = {.started = true, .exit_code = 143},
            .cancelled = true,
        };
    }
};

void wait_until_entered(CancellationAwareTransferPipeline& pipeline) {
    for (int attempt = 0; attempt < 200 && !pipeline.entered.load(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    test_helpers::expect_true("cancellable runner entered", pipeline.entered.load(), "runner did not reach transfer");
}

void wait_until_entered(BlockingTransferPipeline& pipeline) {
    for (int attempt = 0; attempt < 200 && !pipeline.entered.load(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    test_helpers::expect_true("blocking runner entered", pipeline.entered.load(), "runner did not reach transfer");
}

class MetadataMap {
  public:
    std::map<std::string, btrfsbackup::SnapshotMetadata> values;

    std::optional<btrfsbackup::SnapshotMetadata> read(const fs::path& path) const {
        auto found = values.find(path.string());
        if (found == values.end()) {
            return std::nullopt;
        }
        return found->second;
    }
};

void add_snapshot_metadata(
    MetadataMap& metadata,
    const fs::path& path,
    const std::string& uuid,
    const std::string& received_uuid = ""
) {
    metadata.values[path.string()] = btrfsbackup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .uuid = uuid,
        .received_uuid = received_uuid,
    };
}

btrfsbackup::Profile test_profile(const fs::path& root) {
    btrfsbackup::Profile profile{btrfsbackup::ProfileId{"default"}};
    profile.name = "Default backup";
    profile.enabled = true;
    profile.target.device = "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555";
    profile.target.luks_uuid = "11111111-2222-3333-4444-555555555555";
    profile.target.btrfs_uuid = "22222222-3333-4444-5555-666666666666";
    profile.target.partition_uuid = "";
    profile.target.serial = "";
    profile.target.mapper_name = "backup";
    profile.target.mount_point = (root / "target" / "default").string();
    profile.target.mount_unit = "";
    profile.paths.remote_root = (root / "target" / "default" / "snapshots").string();
    profile.paths.incoming_root = (root / "target" / "default" / ".incoming").string();
    profile.settings.incremental_required = false;
    profile.settings.keep_failed_local_snapshot = false;
    profile.settings.remote_retention = 2;
    profile.settings.local_retention = 2;
    btrfsbackup::ProfileSource source{btrfsbackup::SourceId{"root"}};
    source.name = "System";
    source.subvolume = (root / "source" / "root").string();
    source.local_snapshot_dir = (root / "source" / ".snapshots" / "root").string();
    source.remote_subdir = "root";
    source.remote_retention = 2;
    source.local_retention = 2;
    profile.sources = {std::move(source)};
    return profile;
}

btrfsbackup::ApplicationPaths test_application_paths(const fs::path& root) {
    return {
        .state_root = root / "state",
        .status_root = root / "status",
        .history_root = root / "history",
        .target_mount_root = root / "target",
    };
}

void add_home_source(btrfsbackup::Profile& profile, const fs::path& root) {
    btrfsbackup::ProfileSource source{btrfsbackup::SourceId{"home"}};
    source.name = "Home";
    source.subvolume = (root / "source" / "home").string();
    source.local_snapshot_dir = (root / "source" / ".snapshots" / "home").string();
    source.remote_subdir = "home";
    source.remote_retention = 2;
    source.local_retention = 2;
    profile.sources.push_back(std::move(source));
}

void write_profile(const fs::path& config_root, const btrfsbackup::Profile& profile) {
    fs::path config_path = config_root / "btrfs-backup.conf";
    if (!fs::exists(config_path)) {
        test_helpers::write_file(
            config_path,
            "CONFIG_VERSION=1\nTARGET_MOUNT_ROOT=" + fs::path(profile.target.mount_point).parent_path().string() + "\n"
        );
        chmod(config_path.c_str(), 0600);
    }
    fs::path profile_path = config_root / "profiles" / profile.id.value() / "profile.json";
    test_helpers::write_file(profile_path, btrfsbackup::profile_to_json(profile).dump(2));
    chmod(profile_path.c_str(), 0600);
}

void write_application_config(const fs::path& config_root, const fs::path& root) {
    fs::path config_path = config_root / "btrfs-backup.conf";
    std::string content = "CONFIG_VERSION=1\n";
    content += "STATE_ROOT=" + (root / "state").string() + "\n";
    content += "STATUS_ROOT=" + (root / "status").string() + "\n";
    content += "HISTORY_ROOT=" + (root / "history").string() + "\n";
    content += "TARGET_MOUNT_ROOT=" + (root / "target").string() + "\n";
    test_helpers::write_file(config_path, content);
    chmod(config_path.c_str(), 0600);
}

void write_mountinfo(const fs::path& path, const btrfsbackup::Profile& profile) {
    std::string content;
    int mount_id = 21;
    for (const btrfsbackup::ProfileSource& source : profile.sources) {
        content += std::to_string(mount_id++) + " 31 0:20 / " + fs::path(source.subvolume).string() + " rw,relatime - btrfs /dev/source rw\n";
        content += std::to_string(mount_id++) + " 31 0:20 / " + fs::path(source.local_snapshot_dir).string() + " rw,relatime - btrfs /dev/source rw\n";
    }
    content += std::to_string(mount_id) + " 31 0:21 / " + fs::path(profile.target.mount_point).string() + " rw,relatime,nodev,nosuid,noexec,nosymfollow - btrfs /dev/mapper/backup rw\n";
    test_helpers::write_file(path, content);
}

class FixedClock final : public btrfsbackup::IClock {
  public:
    std::string timestamp = "2026-08-23T080000Z";
    std::string today = "2026-08-23";

    std::string snapshot_timestamp() const override {
        return timestamp;
    }
    std::string local_date() const override {
        return today;
    }
    std::string local_timestamp() const override {
        return "2026-08-23T08:00:00+02:00";
    }
};

class FixedRunIdGenerator final : public btrfsbackup::IRunIdGenerator {
  public:
    btrfsbackup::RunId run_id{"20260823T080000Z-shadow"};
    btrfsbackup::RunId generate(const std::string&) override {
        return run_id;
    }
};

struct ServiceFixture {
    btrfsbackup::IBackupRunActionHandler& action_handler;
    btrfsbackup::ITransferPipeline& transfer_pipeline;
    fs::path lock_root;
    btrfsbackup::ApplicationConfig application_config;
    btrfsbackup::SnapshotMetadataReader snapshot_metadata_reader = nullptr;
};

std::string option_value(const std::vector<std::string>& args, const std::string& option, std::string fallback = {}) {
    for (std::size_t index = 0; index + 1 < args.size(); ++index) {
        if (args[index] == option) {
            return args[index + 1];
        }
    }
    return fallback;
}

std::string compact_test_timestamp(const std::string& timestamp) {
    std::string result;
    for (const char character : timestamp) {
        if (character != '-' && character != ':') {
            result.push_back(character);
        }
    }
    return result;
}

int run_runner(
    const fs::path& config_root,
    const std::vector<std::string>& args,
    std::ostream& output,
    ServiceFixture* fixture,
    btrfsbackup::CancellationToken* external_cancellation = nullptr
) {
    if (fixture == nullptr) {
        return btrfsbackup::command::runner(config_root, args, output);
    }
    const btrfsbackup::ProfileId profile_id{option_value(args, "--profile", "default")};
    const btrfsbackup::Profile profile = btrfsbackup::load_profile_by_id(
        config_root,
        std::string(profile_id.value())
    );
    const fs::path mountinfo = option_value(args, "--mountinfo", "/proc/self/mountinfo");
    btrfsbackup::FileProfileRepository profiles(config_root, fixture->application_config);
    btrfsbackup::LinuxMountInspector mounts(mountinfo, [target_uuid = profile.target.btrfs_uuid](const std::string& source) {
        return source.find("/dev/mapper/") == 0 ? target_uuid : "source-btrfs-uuid";
    });
    btrfsbackup::PosixCommandRunner commands;
    btrfsbackup::SystemdTargetMounter target_mounter(mounts, commands);
    test_support::FakeSafeDirectoryRootFactory safe_directories;
    btrfsbackup::DefaultBackupPlanner planner(
        fixture->snapshot_metadata_reader
            ? fixture->snapshot_metadata_reader
            : btrfsbackup::SnapshotMetadataReader{[](const fs::path&) {
                  return std::optional<btrfsbackup::SnapshotMetadata>{};
              }},
        safe_directories
    );
    btrfsbackup::DefaultBackupRunFactory run_factory(
        fixture->action_handler,
        fixture->transfer_pipeline,
        safe_directories
    );
    btrfsbackup::FileBackupRunLeaseProvider leases(fixture->lock_root);
    btrfsbackup::FileRunStateRepository state(fixture->application_config.paths());
    btrfsbackup::FileCancellationMonitor cancellation_monitor(state);
    FixedClock clock;
    clock.timestamp = option_value(args, "--timestamp", clock.timestamp);
    clock.today = option_value(args, "--today", clock.today);
    FixedRunIdGenerator run_ids;
    run_ids.run_id = btrfsbackup::RunId{option_value(
        args,
        "--run-id",
        compact_test_timestamp(clock.timestamp) + "-shadow"
    )};
    btrfsbackup::CancellationToken owned_cancellation;
    btrfsbackup::CancellationToken& cancellation = external_cancellation == nullptr
        ? owned_cancellation
        : *external_cancellation;
    btrfsbackup::BackupService service(
        profiles,
        mounts,
        target_mounter,
        planner,
        run_factory,
        leases,
        state,
        cancellation_monitor,
        clock,
        run_ids,
        cancellation
    );
    return btrfsbackup::command::runner(args, output, service);
}

int run_runner(const fs::path& config_root, const std::vector<std::string>& args, std::ostream& output) {
    return btrfsbackup::command::runner(config_root, args, output);
}

std::string profile_fingerprint(const fs::path& config_root, const btrfsbackup::Profile& profile) {
    return btrfsbackup::compute_config_fingerprint(
        "2.0.0",
        config_root / "profiles" / profile.id.value() / "profile.json",
        {}
    );
}

void write_matching_last_success(const fs::path& config_root, const btrfsbackup::Profile& profile) {
    btrfsbackup::write_success_state(
        config_root.parent_path() / "state" / "profiles" / profile.id.value(),
        btrfsbackup::SuccessState{
            .date = "2026-08-23",
            .timestamp = "2026-08-23T08:00:00+02:00",
            .run_id = "20260823T080000Z-previous",
            .profile_id = std::string(profile.id.value()),
            .profile_name = profile.name,
            .source_count = 1,
            .target_luks_uuid = profile.target.luks_uuid,
            .config_fingerprint = profile_fingerprint(config_root, profile),
        }
    );
}

void test_runner_plan_outputs_shadow_json() {
    fs::path root = test_helpers::test_root("runner-command", "plan");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    std::ostringstream output;
    int result = run_runner(
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
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    std::ostringstream output;
    test_helpers::expect_validation_error("runner target uuid", [&] { (void)run_runner(
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
                                                                      ); }, "Btrfs UUID mismatch");

    fs::remove_all(root);
}

void test_runner_execute_rejects_busy_profile_before_target_access() {
    fs::path root = test_helpers::test_root("runner-command", "execute-profile-busy");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    fs::path lock_root = root / "locks";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    btrfsbackup::FileLock active_profile_lock(
        btrfsbackup::profile_lock_path(lock_root, std::string(profile.id.value()))
    );
    test_helpers::expect_true(
        "active profile lock acquired",
        active_profile_lock.try_acquire(),
        "test setup should acquire profile lock"
    );

    RecordingActionHandler action_handler;
    ConfigurableTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = lock_root,
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };
    std::ostringstream output;
    int result = run_runner(
        config_root,
        {
            "execute",
            "--profile",
            std::string(profile.id.value()),
            "--mountinfo",
            mountinfo.string(),
        },
        output,
        &services
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("profile busy result", std::to_string(result), "1");
    test_helpers::expect_true("profile busy flag", json.at("busy").get<bool>(), "runner should report busy");
    test_helpers::expect_eq(
        "profile busy error",
        json.at("errorCode").get<std::string>(),
        "runner.profile_busy"
    );
    test_helpers::expect_true("profile busy effects", action_handler.calls.empty(), "busy runner must not execute actions");
    test_helpers::expect_true("profile busy transfers", transfer_pipeline.plans.empty(), "busy runner must not transfer");
    test_helpers::expect_true(
        "profile busy status absent",
        !fs::exists(root / "status" / profile.id.value() / "current.json"),
        "busy runner must not replace current status"
    );

    active_profile_lock.release();
    fs::remove_all(root);
}

void test_runner_execute_serializes_shared_target_but_allows_another_target() {
    fs::path root = test_helpers::test_root("runner-command", "execute-target-locks");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile active_profile = test_profile(root);
    btrfsbackup::Profile shared_target_profile = test_profile(root);
    shared_target_profile.id = btrfsbackup::ProfileId{"shared"};
    shared_target_profile.name = "Shared target";
    shared_target_profile.target.mount_point = (root / "target" / "shared").string();
    shared_target_profile.paths.remote_root = (root / "target" / "shared" / "snapshots").string();
    shared_target_profile.paths.incoming_root = (root / "target" / "shared" / ".incoming").string();
    btrfsbackup::Profile other_target_profile = test_profile(root);
    other_target_profile.id = btrfsbackup::ProfileId{"other"};
    other_target_profile.name = "Other target";
    other_target_profile.target.luks_uuid = "33333333-4444-5555-6666-777777777777";
    other_target_profile.target.btrfs_uuid = "44444444-5555-6666-7777-888888888888";
    other_target_profile.target.mount_point = (root / "target" / "other").string();
    other_target_profile.paths.remote_root = (root / "target" / "other" / "snapshots").string();
    other_target_profile.paths.incoming_root = (root / "target" / "other" / ".incoming").string();
    fs::create_directories(root / "target" / "shared" / "snapshots" / "root");
    fs::create_directories(root / "target" / "shared" / ".incoming");
    fs::create_directories(root / "target" / "other" / "snapshots" / "root");
    fs::create_directories(root / "target" / "other" / ".incoming");

    fs::path config_root = root / "config";
    fs::path active_mountinfo = root / "active-mountinfo";
    fs::path shared_mountinfo = root / "shared-mountinfo";
    fs::path other_mountinfo = root / "other-mountinfo";
    fs::path lock_root = root / "locks";
    write_profile(config_root, active_profile);
    write_profile(config_root, shared_target_profile);
    write_profile(config_root, other_target_profile);
    write_mountinfo(active_mountinfo, active_profile);
    write_mountinfo(shared_mountinfo, shared_target_profile);
    write_mountinfo(other_mountinfo, other_target_profile);

    RecordingActionHandler active_action_handler;
    BlockingTransferPipeline active_transfer_pipeline;
    ServiceFixture active_services{
        .action_handler = active_action_handler,
        .transfer_pipeline = active_transfer_pipeline,
        .lock_root = lock_root,
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };
    std::ostringstream active_output;
    int active_result = -1;
    std::exception_ptr active_error;
    std::thread active_runner([&] {
        try {
            active_result = run_runner(
                config_root,
                {
                    "execute",
                    "--profile",
                    std::string(active_profile.id.value()),
                    "--timestamp",
                    "2026-08-23T080000Z",
                    "--run-id",
                    "20260823T080000Z-active",
                    "--today",
                    "2026-08-23",
                    "--mountinfo",
                    active_mountinfo.string(),
                    "--mount-uuid",
                    "/dev/source",
                    "source-fs",
                    "--mount-uuid",
                    "/dev/mapper/backup",
                    active_profile.target.btrfs_uuid,
                },
                active_output,
                &active_services
            );
        } catch (...) {
            active_error = std::current_exception();
        }
    });
    wait_until_entered(active_transfer_pipeline);

    RecordingActionHandler same_profile_action_handler;
    ConfigurableTransferPipeline same_profile_transfer_pipeline;
    ServiceFixture same_profile_services{
        .action_handler = same_profile_action_handler,
        .transfer_pipeline = same_profile_transfer_pipeline,
        .lock_root = lock_root,
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };
    std::ostringstream same_profile_output;
    int same_profile_result = run_runner(
        config_root,
        {
            "execute",
            "--profile",
            std::string(active_profile.id.value()),
            "--mountinfo",
            active_mountinfo.string(),
        },
        same_profile_output,
        &same_profile_services
    );

    btrfsbackup::Json same_profile_json = btrfsbackup::Json::parse(same_profile_output.str());
    test_helpers::expect_eq("concurrent profile result", std::to_string(same_profile_result), "1");
    test_helpers::expect_eq(
        "concurrent profile error",
        same_profile_json.at("errorCode").get<std::string>(),
        "runner.profile_busy"
    );
    test_helpers::expect_true("concurrent profile effects", same_profile_action_handler.calls.empty(), "second runner must not execute actions");
    test_helpers::expect_true("concurrent profile transfers", same_profile_transfer_pipeline.plans.empty(), "second runner must not transfer");
    btrfsbackup::Json active_status = btrfsbackup::load_json_file(
        root / "status" / active_profile.id.value() / "current.json"
    );
    test_helpers::expect_eq(
        "active status preserved",
        active_status.at("state").get<std::string>(),
        "running"
    );

    RecordingActionHandler shared_action_handler;
    ConfigurableTransferPipeline shared_transfer_pipeline;
    ServiceFixture shared_services{
        .action_handler = shared_action_handler,
        .transfer_pipeline = shared_transfer_pipeline,
        .lock_root = lock_root,
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };
    std::ostringstream shared_output;
    int shared_result = run_runner(
        config_root,
        {
            "execute",
            "--profile",
            std::string(shared_target_profile.id.value()),
            "--mountinfo",
            shared_mountinfo.string(),
        },
        shared_output,
        &shared_services
    );

    btrfsbackup::Json shared_json = btrfsbackup::Json::parse(shared_output.str());
    test_helpers::expect_eq("target busy result", std::to_string(shared_result), "1");
    test_helpers::expect_eq(
        "target busy error",
        shared_json.at("errorCode").get<std::string>(),
        "runner.target_busy"
    );
    test_helpers::expect_true("target busy effects", shared_action_handler.calls.empty(), "busy target must not execute actions");
    test_helpers::expect_true(
        "target busy status absent",
        !fs::exists(root / "status" / shared_target_profile.id.value() / "current.json"),
        "busy runner must not write status for the rejected profile"
    );

    RecordingActionHandler other_action_handler;
    ConfigurableTransferPipeline other_transfer_pipeline;
    ServiceFixture other_services{
        .action_handler = other_action_handler,
        .transfer_pipeline = other_transfer_pipeline,
        .lock_root = lock_root,
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };
    std::ostringstream other_output;
    int other_result = run_runner(
        config_root,
        {
            "execute",
            "--profile",
            std::string(other_target_profile.id.value()),
            "--timestamp",
            "2026-08-23T080000Z",
            "--run-id",
            "20260823T080000Z-other",
            "--mountinfo",
            other_mountinfo.string(),
            "--mount-uuid",
            "/dev/source",
            "source-fs",
            "--mount-uuid",
            "/dev/mapper/backup",
            other_target_profile.target.btrfs_uuid,
        },
        other_output,
        &other_services
    );

    test_helpers::expect_eq("other target result", std::to_string(other_result), "0");
    test_helpers::expect_true("other target effects", !other_action_handler.calls.empty(), "other target should execute");
    test_helpers::expect_eq(
        "other target transfers",
        std::to_string(other_transfer_pipeline.plans.size()),
        "1"
    );

    active_transfer_pipeline.allow_finish.store(true);
    active_runner.join();
    if (active_error != nullptr) {
        std::rethrow_exception(active_error);
    }
    test_helpers::expect_eq("active runner result", std::to_string(active_result), "0");
    fs::remove_all(root);
}

void test_runner_execute_uses_injected_services_and_writes_state() {
    fs::path root = test_helpers::test_root("runner-command", "execute-injected");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionHandler action_handler;
    ConfigurableTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };

    std::ostringstream output;
    int result = run_runner(
        config_root,
        {
            "execute",
            "--profile",
            "default",
            "--timestamp",
            "2026-08-23T080000Z",
            "--run-id",
            "20260823T080000Z-123-456",
            "--today",
            "2026-08-23",
            "--mountinfo",
            mountinfo.string(),
            "--mount-uuid",
            "/dev/source",
            "source-fs",
            "--mount-uuid",
            "/dev/mapper/backup",
            profile.target.btrfs_uuid,
        },
        output,
        &services
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("execute result", std::to_string(result), "0");
    test_helpers::expect_eq("execute mode", json.at("mode").get<std::string>(), "cpp-execute");
    test_helpers::expect_true("execute completed", json.at("completed").get<bool>(), "run should complete");
    test_helpers::expect_true("execute not skipped", !json.at("skipped").get<bool>(), "first run should execute");
    test_helpers::expect_eq("execute source count", std::to_string(json.at("sources").size()), "1");
    test_helpers::expect_true("execute full stream", !json.at("sources").at(0).at("incremental").get<bool>(), "first run should be full");
    test_helpers::expect_eq("execute transfer count", std::to_string(transfer_pipeline.plans.size()), "1");
    test_helpers::expect_true("execute effects", !action_handler.calls.empty(), "expected non-transfer effects");

    fs::path checkpoint = root / "state" / "profiles" / "default" / "checkpoint.json";
    fs::path current = root / "status" / "default" / "current.json";
    fs::path history = root / "history" / "default" / "20260823T080000Z-123-456.json";
    test_helpers::expect_true("checkpoint exists", fs::is_regular_file(checkpoint), "missing checkpoint");
    test_helpers::expect_true("current exists", fs::is_regular_file(current), "missing current status");
    test_helpers::expect_true("history exists", fs::is_regular_file(history), "missing history");
    test_helpers::expect_true(
        "current succeeded",
        btrfsbackup::load_json_file(current).at("state") == "succeeded",
        "current status should be succeeded"
    );
    test_helpers::expect_true(
        "last success matches",
        btrfsbackup::last_success_matches(
            root / "state" / "profiles" / "default",
            "2026-08-23",
            profile.target.luks_uuid,
            profile_fingerprint(config_root, profile)
        ),
        "successful runner should write daily-limit state"
    );

    fs::remove_all(root);
}

void test_runner_execute_daily_limit_skips_matching_success() {
    fs::path root = test_helpers::test_root("runner-command", "execute-daily-limit-skip");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_matching_last_success(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionHandler action_handler;
    ConfigurableTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };

    std::ostringstream output;
    int result = run_runner(
        config_root,
        {
            "execute",
            "--profile",
            "default",
            "--today",
            "2026-08-23",
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
        output,
        &services
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("daily skip result", std::to_string(result), "0");
    test_helpers::expect_true("daily skip completed", json.at("completed").get<bool>(), "skip should be successful");
    test_helpers::expect_true("daily skip flag", json.at("skipped").get<bool>(), "matching success should skip");
    test_helpers::expect_eq("daily skip actions", std::to_string(action_handler.calls.size()), "0");
    test_helpers::expect_eq("daily skip transfers", std::to_string(transfer_pipeline.plans.size()), "0");

    fs::path current = root / "status" / "default" / "current.json";
    fs::path history = root / "history" / "default" / "20260823T080000Z-123-456.json";
    test_helpers::expect_true("daily skip current exists", fs::is_regular_file(current), "missing skipped current status");
    test_helpers::expect_true("daily skip history exists", fs::is_regular_file(history), "missing skipped history");
    test_helpers::expect_true(
        "daily skip current",
        btrfsbackup::load_json_file(current).at("state") == "skipped",
        "current status should be skipped"
    );
    test_helpers::expect_true(
        "daily skip history",
        btrfsbackup::load_json_file(history).at("state") == "skipped",
        "history should be skipped"
    );

    fs::remove_all(root);
}

void test_runner_execute_force_ignores_daily_limit() {
    fs::path root = test_helpers::test_root("runner-command", "execute-daily-limit-force");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_matching_last_success(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionHandler action_handler;
    ConfigurableTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };

    std::ostringstream output;
    int result = run_runner(
        config_root,
        {
            "execute",
            "--force",
            "--profile",
            "default",
            "--today",
            "2026-08-23",
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
        output,
        &services
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("force result", std::to_string(result), "0");
    test_helpers::expect_true("force completed", json.at("completed").get<bool>(), "forced run should complete");
    test_helpers::expect_true("force not skipped", !json.at("skipped").get<bool>(), "forced run should bypass daily limit");
    test_helpers::expect_eq("force transfers", std::to_string(transfer_pipeline.plans.size()), "1");
    test_helpers::expect_true("force actions", !action_handler.calls.empty(), "forced run should execute actions");

    fs::remove_all(root);
}

void test_runner_execute_validate_builds_plan_without_effects() {
    fs::path root = test_helpers::test_root("runner-command", "execute-validate");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionHandler action_handler;
    ConfigurableTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };

    std::ostringstream output;
    int result = run_runner(
        config_root,
        {
            "execute",
            "--validate",
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
        output,
        &services
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("validate result", std::to_string(result), "0");
    test_helpers::expect_eq("validate mode", json.at("mode").get<std::string>(), "cpp-validate");
    test_helpers::expect_true("validate completed", json.at("completed").get<bool>(), "validation should complete");
    test_helpers::expect_eq("validate transfer count", std::to_string(transfer_pipeline.plans.size()), "0");
    test_helpers::expect_true("validate effects", action_handler.calls.empty(), "validation should not execute actions");
    test_helpers::expect_true("validate checkpoint absent", !fs::exists(root / "state" / "profiles" / "default" / "checkpoint.json"), "validation should not write checkpoint");

    fs::remove_all(root);
}

void test_runner_execute_transfer_failure_writes_failed_status() {
    fs::path root = test_helpers::test_root("runner-command", "execute-transfer-failure");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionHandler action_handler;
    ConfigurableTransferPipeline transfer_pipeline;
    transfer_pipeline.next_result.producer.exit_code = 7;
    transfer_pipeline.next_result.producer.diagnostics = "send failed";
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };

    std::ostringstream output;
    test_helpers::expect_validation_error("execute transfer failure", [&] { (void)run_runner(
                                                                                config_root,
                                                                                {
                                                                                    "execute",
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
                                                                                output,
                                                                                &services
                                                                            ); }, "producer failed with exit code 7");

    fs::path checkpoint = root / "state" / "profiles" / "default" / "checkpoint.json";
    fs::path current = root / "status" / "default" / "current.json";
    fs::path history = root / "history" / "default" / "20260823T080000Z-123-456.json";
    btrfsbackup::Json current_json = btrfsbackup::load_json_file(current);
    test_helpers::expect_true("failed checkpoint exists", fs::is_regular_file(checkpoint), "missing checkpoint before failure");
    test_helpers::expect_true(
        "failed checkpoint action",
        btrfsbackup::load_json_file(checkpoint).at("action") == "create-snapshot",
        "failed send-receive should not be checkpointed"
    );
    test_helpers::expect_true(
        "failed current",
        current_json.at("state") == "failed",
        "current status should fail"
    );
    test_helpers::expect_eq("failed transfer public error code", current_json.at("errorCode").get<std::string>(), "backup.failed");
    test_helpers::expect_eq(
        "failed transfer private error code",
        btrfsbackup::load_json_file(history).at("errorCode").get<std::string>(),
        "transfer.producer_failed"
    );
    test_helpers::expect_true(
        "failed history",
        btrfsbackup::load_json_file(history).at("state") == "failed",
        "history should fail"
    );

    fs::remove_all(root);
}

void test_runner_execute_commit_failure_writes_failed_status() {
    fs::path root = test_helpers::test_root("runner-command", "execute-commit-failure");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionHandler action_handler;
    action_handler.should_throw = true;
    action_handler.throw_on = btrfsbackup::BackupRunActionKind::CommitReceived;
    ConfigurableTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };

    std::ostringstream output;
    test_helpers::expect_validation_error("execute commit failure", [&] { (void)run_runner(
                                                                              config_root,
                                                                              {
                                                                                  "execute",
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
                                                                              output,
                                                                              &services
                                                                          ); }, "injected action failure");

    fs::path checkpoint = root / "state" / "profiles" / "default" / "checkpoint.json";
    fs::path current = root / "status" / "default" / "current.json";
    fs::path history = root / "history" / "default" / "20260823T080000Z-123-456.json";
    test_helpers::expect_true(
        "commit failure checkpoint action",
        btrfsbackup::load_json_file(checkpoint).at("action") == "verify-received",
        "failed commit should not be checkpointed"
    );
    test_helpers::expect_true(
        "commit failure current",
        btrfsbackup::load_json_file(current).at("state") == "failed",
        "current status should fail"
    );
    test_helpers::expect_true(
        "commit failure phase",
        btrfsbackup::load_json_file(history).at("phase") == "commit-received",
        "history should identify commit phase"
    );

    fs::remove_all(root);
}

void test_runner_execute_verify_failure_writes_failed_status() {
    fs::path root = test_helpers::test_root("runner-command", "execute-verify-failure");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionHandler action_handler;
    action_handler.should_throw = true;
    action_handler.throw_on = btrfsbackup::BackupRunActionKind::VerifyReceived;
    ConfigurableTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };

    std::ostringstream output;
    test_helpers::expect_validation_error("execute verify failure", [&] { (void)run_runner(
                                                                              config_root,
                                                                              {
                                                                                  "execute",
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
                                                                              output,
                                                                              &services
                                                                          ); }, "injected action failure");

    fs::path checkpoint = root / "state" / "profiles" / "default" / "checkpoint.json";
    fs::path current = root / "status" / "default" / "current.json";
    fs::path history = root / "history" / "default" / "20260823T080000Z-123-456.json";
    test_helpers::expect_true(
        "verify failure checkpoint action",
        btrfsbackup::load_json_file(checkpoint).at("action") == "send-receive",
        "failed verify should not be checkpointed"
    );
    test_helpers::expect_true(
        "verify failure current",
        btrfsbackup::load_json_file(current).at("state") == "failed",
        "current status should fail"
    );
    test_helpers::expect_true(
        "verify failure phase",
        btrfsbackup::load_json_file(history).at("phase") == "verify-received",
        "history should identify verify phase"
    );

    fs::remove_all(root);
}

void test_runner_execute_multi_source_success() {
    fs::path root = test_helpers::test_root("runner-command", "execute-multi-source");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / "home");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "home");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "home");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    add_home_source(profile, root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionHandler action_handler;
    ConfigurableTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };

    std::ostringstream output;
    int result = run_runner(
        config_root,
        {
            "execute",
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
        output,
        &services
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("multi execute result", std::to_string(result), "0");
    test_helpers::expect_true("multi completed", json.at("completed").get<bool>(), "run should complete");
    test_helpers::expect_eq("multi transfers", std::to_string(transfer_pipeline.plans.size()), "2");
    test_helpers::expect_true("root actions", std::find(action_handler.calls.begin(), action_handler.calls.end(), "root:" + action_name(btrfsbackup::BackupRunActionKind::CreateSnapshot)) != action_handler.calls.end(), "missing root create action");
    test_helpers::expect_true("home actions", std::find(action_handler.calls.begin(), action_handler.calls.end(), "home:" + action_name(btrfsbackup::BackupRunActionKind::CreateSnapshot)) != action_handler.calls.end(), "missing home create action");

    btrfsbackup::Json current = btrfsbackup::load_json_file(root / "status" / "default" / "current.json");
    test_helpers::expect_true("multi status", current.at("state") == "succeeded", "status should succeed");
    test_helpers::expect_true("multi source hidden", !current.contains("sourceCount"), "public status exposes source count");

    fs::remove_all(root);
}

void test_runner_execute_incremental_uses_selected_parent() {
    fs::path root = test_helpers::test_root("runner-command", "execute-incremental");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path local_parent = root / "source" / ".snapshots" / "root" / "root-2026-08-22T080000Z";
    fs::path remote_parent = root / "target" / "default" / "snapshots" / "root" / "root-2026-08-22T080000Z";
    fs::create_directories(local_parent);
    fs::create_directories(remote_parent);

    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    MetadataMap metadata;
    add_snapshot_metadata(metadata, local_parent, "parent-uuid");
    add_snapshot_metadata(metadata, remote_parent, "remote-parent-uuid", "parent-uuid");

    RecordingActionHandler action_handler;
    ConfigurableTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
        .snapshot_metadata_reader = [&metadata](const fs::path& path) {
            return metadata.read(path);
        },
    };

    std::ostringstream output;
    int result = run_runner(
        config_root,
        {
            "execute",
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
        output,
        &services
    );

    test_helpers::expect_eq("incremental result", std::to_string(result), "0");
    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_true("incremental output", json.at("sources").at(0).at("incremental").get<bool>(), "runner output should identify incremental transfer");
    test_helpers::expect_eq("incremental output parent", json.at("sources").at(0).at("parentPath").get<std::string>(), local_parent.string());
    test_helpers::expect_eq("incremental transfers", std::to_string(transfer_pipeline.plans.size()), "1");
    const std::vector<std::string>& send_argv = transfer_pipeline.plans.at(0).producer_argv;
    test_helpers::expect_eq("incremental protocol flag", send_argv.at(2), "--proto");
    test_helpers::expect_eq("incremental protocol", send_argv.at(3), "2");
    test_helpers::expect_eq("incremental compressed data", send_argv.at(4), "--compressed-data");
    test_helpers::expect_eq("incremental parent flag", send_argv.at(5), "-p");
    test_helpers::expect_eq("incremental parent path", send_argv.at(6), local_parent.string());

    fs::remove_all(root);
}

void test_runner_execute_retention_plans_local_and_remote_deletes() {
    fs::path root = test_helpers::test_root("runner-command", "execute-retention");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    profile.sources.at(0).local_retention = 2;
    profile.sources.at(0).remote_retention = 2;
    fs::path local_old = root / "source" / ".snapshots" / "root" / "root-2026-08-20T080000Z";
    fs::path local_keep = root / "source" / ".snapshots" / "root" / "root-2026-08-22T080000Z";
    fs::path remote_old = root / "target" / "default" / "snapshots" / "root" / "root-2026-08-20T080000Z";
    fs::path remote_keep = root / "target" / "default" / "snapshots" / "root" / "root-2026-08-22T080000Z";
    fs::create_directories(local_old);
    fs::create_directories(local_keep);
    fs::create_directories(remote_old);
    fs::create_directories(remote_keep);

    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    MetadataMap metadata;
    add_snapshot_metadata(metadata, local_old, "local-old-uuid");
    add_snapshot_metadata(metadata, local_keep, "local-keep-uuid");
    add_snapshot_metadata(metadata, remote_old, "remote-old-uuid", "local-old-uuid");
    add_snapshot_metadata(metadata, remote_keep, "remote-keep-uuid", "local-keep-uuid");

    RecordingActionHandler action_handler;
    ConfigurableTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
        .snapshot_metadata_reader = [&metadata](const fs::path& path) {
            return metadata.read(path);
        },
    };

    std::ostringstream output;
    int result = run_runner(
        config_root,
        {
            "execute",
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
        output,
        &services
    );

    test_helpers::expect_eq("retention result", std::to_string(result), "0");
    test_helpers::expect_true("remote retention action", std::find(action_handler.calls.begin(), action_handler.calls.end(), "root:" + action_name(btrfsbackup::BackupRunActionKind::ApplyRemoteRetention)) != action_handler.calls.end(), "missing remote retention action");
    test_helpers::expect_true("local retention action", std::find(action_handler.calls.begin(), action_handler.calls.end(), "root:" + action_name(btrfsbackup::BackupRunActionKind::ApplyLocalRetention)) != action_handler.calls.end(), "missing local retention action");
    test_helpers::expect_eq("remote retention delete count", std::to_string(action_handler.remote_retention_deletes.size()), "1");
    test_helpers::expect_eq("remote retention delete", action_handler.remote_retention_deletes.at(0), remote_old.string());
    test_helpers::expect_eq("local retention delete count", std::to_string(action_handler.local_retention_deletes.size()), "1");
    test_helpers::expect_eq("local retention delete", action_handler.local_retention_deletes.at(0), local_old.string());

    fs::remove_all(root);
}

void test_runner_execute_pending_recovery_deletes_orphan() {
    fs::path root = test_helpers::test_root("runner-command", "execute-pending-recovery");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path pending = root / "source" / ".snapshots" / "root" / "root-2026-08-22T080000Z";
    fs::create_directories(pending);

    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);
    btrfsbackup::write_pending_marker(
        root / "state" / "profiles" / "default",
        btrfsbackup::PendingMarker{
            .source_name = "root",
            .local_snapshot_path = pending.string(),
            .final_snapshot_path = (root / "target" / "default" / "snapshots" / "root" / pending.filename()).string(),
            .run_id = "20260822T080000Z-123-456",
            .timestamp = "2026-08-22T08:00:00Z",
        }
    );

    MetadataMap metadata;
    add_snapshot_metadata(metadata, pending, "orphan-uuid");

    RecordingActionHandler action_handler;
    ConfigurableTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
        .snapshot_metadata_reader = [&metadata](const fs::path& path) {
            return metadata.read(path);
        },
    };

    std::ostringstream output;
    int result = run_runner(
        config_root,
        {
            "execute",
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
        output,
        &services
    );

    test_helpers::expect_eq("pending recovery result", std::to_string(result), "0");
    test_helpers::expect_true("recover action first", !action_handler.calls.empty(), "expected action calls");
    test_helpers::expect_eq(
        "recover first action",
        action_handler.calls.at(0),
        "root:" + action_name(btrfsbackup::BackupRunActionKind::RecoverPending)
    );
    test_helpers::expect_eq("pending delete count", std::to_string(action_handler.recovered_pending_paths.size()), "1");
    test_helpers::expect_eq("pending delete path", action_handler.recovered_pending_paths.at(0), pending.string());

    fs::remove_all(root);
}

void test_runner_cancel_writes_cancel_request_without_target_mount() {
    fs::path root = test_helpers::test_root("runner-command", "cancel");
    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    write_profile(config_root, profile);
    write_application_config(config_root, root);

    std::ostringstream output;
    int result = run_runner(
        config_root,
        {
            "cancel",
            "--profile",
            "default",
        },
        output
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    fs::path profile_state_dir = root / "state" / "profiles" / "default";
    test_helpers::expect_eq("cancel result", std::to_string(result), "0");
    test_helpers::expect_eq("cancel mode", json.at("mode").get<std::string>(), "cpp-cancel");
    test_helpers::expect_true("cancel requested json", json.at("cancelRequested").get<bool>(), "cancel should be requested");
    test_helpers::expect_true(
        "cancel request exists",
        btrfsbackup::cancel_requested(profile_state_dir),
        "cancel request file missing"
    );

    fs::remove_all(root);
}

void test_runner_execute_honors_cancel_request_during_transfer() {
    fs::path root = test_helpers::test_root("runner-command", "execute-cancel");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionHandler action_handler;
    ConfigurableTransferPipeline transfer_pipeline;
    fs::path profile_state_dir = root / "state" / "profiles" / "default";
    transfer_pipeline.request_cancel_path = profile_state_dir;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };

    std::ostringstream output;
    int result = run_runner(
        config_root,
        {
            "execute",
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
        output,
        &services
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("execute cancel result", std::to_string(result), "1");
    test_helpers::expect_true("execute cancel incomplete", !json.at("completed").get<bool>(), "cancelled run should not complete");
    test_helpers::expect_true("execute cancel flag", json.at("cancelled").get<bool>(), "cancelled flag should be true");

    fs::path current = root / "status" / "default" / "current.json";
    btrfsbackup::Json current_json = btrfsbackup::load_json_file(current);
    test_helpers::expect_eq("execute cancel status state", current_json.at("state").get<std::string>(), "cancelled");
    test_helpers::expect_eq("execute cancel code", current_json.at("errorCode").get<std::string>(), "backup.cancelled");
    test_helpers::expect_true(
        "execute cancel request cleaned",
        !btrfsbackup::cancel_requested(profile_state_dir),
        "handled cancellation request should be removed"
    );

    fs::remove_all(root);
}

void test_runner_execute_handles_sigint_as_cancelled_with_recovery_marker() {
    fs::path root = test_helpers::test_root("runner-command", "execute-sigint");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    fs::path profile_state_dir = root / "state" / "profiles" / "default";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionHandler action_handler;
    action_handler.write_pending_on_snapshot = true;
    action_handler.pending_state_dir = profile_state_dir;
    CancellationAwareTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::ApplicationConfig(test_application_paths(root)),
    };
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::TerminationSignalMonitor termination_signals(cancellation);
    std::ostringstream output;
    int result = -1;
    std::exception_ptr runner_error;
    std::thread runner([&] {
        try {
            result = run_runner(
                config_root,
                {
                    "execute",
                    "--profile",
                    "default",
                    "--timestamp",
                    "2026-08-23T080000Z",
                    "--run-id",
                    "20260823T080000Z-sigint",
                    "--mountinfo",
                    mountinfo.string(),
                    "--mount-uuid",
                    "/dev/source",
                    "source-fs",
                    "--mount-uuid",
                    "/dev/mapper/backup",
                    profile.target.btrfs_uuid,
                },
                output,
                &services,
                &cancellation
            );
        } catch (...) {
            runner_error = std::current_exception();
        }
    });

    wait_until_entered(transfer_pipeline);
    test_helpers::expect_eq("send runner SIGINT", std::to_string(kill(getpid(), SIGINT)), "0");
    runner.join();
    if (runner_error != nullptr) {
        std::rethrow_exception(runner_error);
    }

    btrfsbackup::Json run = btrfsbackup::Json::parse(output.str());
    btrfsbackup::Json current = btrfsbackup::load_json_file(root / "status" / "default" / "current.json");
    test_helpers::expect_eq("SIGINT runner result", std::to_string(result), "1");
    test_helpers::expect_true("SIGINT cancelled result", run.at("cancelled").get<bool>(), "run should report cancellation");
    test_helpers::expect_eq("SIGINT status state", current.at("state").get<std::string>(), "cancelled");
    test_helpers::expect_eq("SIGINT status code", current.at("errorCode").get<std::string>(), "backup.cancelled");
    test_helpers::expect_true(
        "SIGINT recovery marker",
        fs::is_regular_file(profile_state_dir / "pending-root"),
        "cancelled transfer must retain the pending marker"
    );
    test_helpers::expect_true(
        "SIGINT no last success",
        !fs::exists(profile_state_dir / "last-success"),
        "cancelled run must not write last-success"
    );

    fs::remove_all(root);
}

} // namespace

int main() {
    test_runner_plan_outputs_shadow_json();
    test_runner_plan_validates_target_mount();
    test_runner_execute_rejects_busy_profile_before_target_access();
    test_runner_execute_serializes_shared_target_but_allows_another_target();
    test_runner_execute_uses_injected_services_and_writes_state();
    test_runner_execute_daily_limit_skips_matching_success();
    test_runner_execute_force_ignores_daily_limit();
    test_runner_execute_validate_builds_plan_without_effects();
    test_runner_execute_transfer_failure_writes_failed_status();
    test_runner_execute_commit_failure_writes_failed_status();
    test_runner_execute_verify_failure_writes_failed_status();
    test_runner_execute_multi_source_success();
    test_runner_execute_incremental_uses_selected_parent();
    test_runner_execute_retention_plans_local_and_remote_deletes();
    test_runner_execute_pending_recovery_deletes_orphan();
    test_runner_cancel_writes_cancel_request_without_target_mount();
    test_runner_execute_honors_cancel_request_during_transfer();
    test_runner_execute_handles_sigint_as_cancelled_with_recovery_marker();

    return test_helpers::finish("runner command tests");
}
