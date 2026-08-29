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
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <cli/runner_command.hpp>
#include <cli/backup_tool.hpp>
#include <backup/action_handlers/backup_run_action_handler.hpp>
#include <backup/backup_discovery.hpp>
#include <backup/backup_plan_builder.hpp>
#include <backup/backup_preflight.hpp>
#include <backup/default_backup_run_factory.hpp>
#include <backup/linked_cancellation_monitor.hpp>
#include <state/file_run_state_repository.hpp>
#include <state/file_pending_marker_store.hpp>
#include <config/profile_fingerprint.hpp>
#include <platform/linux/file_lock.hpp>
#include <platform/linux/file_backup_run_lease_provider.hpp>
#include <platform/linux/posix_durable_file_operations.hpp>
#include <platform/linux/posix_command_runner.hpp>
#include <platform/linux/systemd_target_manager.hpp>
#include <platform/linux/mount_info.hpp>
#include <config/model/json.hpp>
#include <config/model/json_io.hpp>
#include <config/model/profile.hpp>
#include <config/model/profile_document.hpp>
#include <platform/linux/config/profile_repository.hpp>
#include <state/run_state.hpp>

#include "support/fake_safe_directory.hpp"
#include "support/validation_test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

std::string action_name(btrfsbackup::backup::BackupRunActionKind kind) {
    return std::to_string(static_cast<int>(kind));
}

class RecordingActionHandler final : public btrfsbackup::backup::IBackupRunActionHandler {
  public:
    btrfsbackup::platform::linux::PosixDurableFileOperations durable_files;
    std::vector<std::string> calls;
    std::vector<std::string> local_retention_deletes;
    std::vector<std::string> remote_retention_deletes;
    std::vector<std::string> recovered_pending_paths;
    btrfsbackup::backup::BackupRunActionKind throw_on = btrfsbackup::backup::BackupRunActionKind::CleanupSource;
    bool should_throw = false;
    bool write_pending_on_snapshot = false;
    fs::path pending_state_dir;
    std::string pending_timestamp = "2026-08-23T08:00:00Z";

    void handle(
        const btrfsbackup::backup::BackupRunAction& action,
        const btrfsbackup::backup::BackupRunPlan& plan,
        btrfsbackup::CancellationToken&
    ) override {
        const btrfsbackup::backup::BackupRunActionKind kind = btrfsbackup::backup::backup_run_action_kind(action);
        calls.push_back(
            std::string(btrfsbackup::backup::backup_run_action_source_id(action).value()) + ":" + action_name(kind)
        );
        if (const auto* retention = std::get_if<btrfsbackup::backup::ApplyLocalRetentionAction>(&action)) {
            for (const btrfsbackup::backup::SnapshotInfo& snapshot : retention->plan.delete_snapshots) {
                local_retention_deletes.push_back(snapshot.path.string());
            }
        }
        if (const auto* retention = std::get_if<btrfsbackup::backup::ApplyRemoteRetentionAction>(&action)) {
            for (const btrfsbackup::backup::SnapshotInfo& snapshot : retention->plan.delete_snapshots) {
                remote_retention_deletes.push_back(snapshot.path.string());
            }
        }
        if (const auto* recovery = std::get_if<btrfsbackup::backup::RecoverPendingAction>(&action);
            recovery != nullptr && recovery->recovery.delete_local_snapshot) {
            recovered_pending_paths.push_back(recovery->recovery.local_snapshot_path.string());
        }
        if (const auto* snapshot = std::get_if<btrfsbackup::backup::CreateSnapshotAction>(&action);
            write_pending_on_snapshot && snapshot != nullptr) {
            btrfsbackup::state::write_pending_marker(
                durable_files,
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

class DelegatingActionHandler final : public btrfsbackup::backup::IBackupRunActionHandler {
  public:
    explicit DelegatingActionHandler(btrfsbackup::backup::IBackupRunActionHandler& delegate)
        : delegate_(delegate) {
    }

    void handle(
        const btrfsbackup::backup::BackupRunAction& action,
        const btrfsbackup::backup::BackupRunPlan& plan,
        btrfsbackup::CancellationToken& cancellation
    ) override {
        delegate_.handle(action, plan, cancellation);
    }

  private:
    btrfsbackup::backup::IBackupRunActionHandler& delegate_;
};

class DelegatingActionHandlerFactory final
    : public btrfsbackup::backup::IBackupRunActionHandlerFactory {
  public:
    explicit DelegatingActionHandlerFactory(btrfsbackup::backup::IBackupRunActionHandler& delegate)
        : delegate_(delegate) {
    }

    std::unique_ptr<btrfsbackup::backup::IBackupRunActionHandler> create(
        const btrfsbackup::backup::BackupRunPlan&
    ) override {
        return std::make_unique<DelegatingActionHandler>(delegate_);
    }

  private:
    btrfsbackup::backup::IBackupRunActionHandler& delegate_;
};

class ConfigurableTransferPipeline final : public btrfsbackup::backup::transfer::ITransferPipeline {
  public:
    btrfsbackup::platform::linux::PosixDurableFileOperations durable_files;
    std::vector<btrfsbackup::backup::transfer::TransferPipelinePlan> plans;
    btrfsbackup::backup::transfer::TransferResult next_result{
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
    std::optional<btrfsbackup::RunId> request_cancel_run_id;

    btrfsbackup::backup::transfer::TransferResult run(
        const btrfsbackup::backup::transfer::TransferPipelinePlan& plan,
        btrfsbackup::backup::transfer::ITransferEventSink& events,
        btrfsbackup::CancellationToken& cancellation
    ) override {
        plans.push_back(plan);
        events.on_transfer_event({
            .kind = btrfsbackup::backup::transfer::TransferEventKind::Progress,
            .bytes_transferred = 1024,
            .bytes_produced = 1024,
            .delta_bytes = 1024,
            .elapsed_ms = 1000,
            .speed_bps = 1024,
        });
        if (request_cancel_path.has_value() && request_cancel_run_id.has_value()) {
            btrfsbackup::state::write_cancel_request(
                durable_files,
                *request_cancel_path,
                *request_cancel_run_id
            );
            for (int i = 0; i < 20 && !cancellation.cancellation_requested(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            next_result.cancelled = cancellation.cancellation_requested();
        }
        return next_result;
    }
};

class BlockingTransferPipeline final : public btrfsbackup::backup::transfer::ITransferPipeline {
  public:
    std::atomic_bool entered = false;
    std::atomic_bool allow_finish = false;

    btrfsbackup::backup::transfer::TransferResult run(
        const btrfsbackup::backup::transfer::TransferPipelinePlan&,
        btrfsbackup::backup::transfer::ITransferEventSink&,
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

class CancellationAwareTransferPipeline final : public btrfsbackup::backup::transfer::ITransferPipeline {
  public:
    std::atomic_bool entered = false;

    btrfsbackup::backup::transfer::TransferResult run(
        const btrfsbackup::backup::transfer::TransferPipelinePlan&,
        btrfsbackup::backup::transfer::ITransferEventSink&,
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
    std::map<std::string, btrfsbackup::backup::SnapshotMetadata> values;

    std::optional<btrfsbackup::backup::SnapshotMetadata> read(const fs::path& path) const {
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
    metadata.values[path.string()] = btrfsbackup::backup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .uuid = uuid,
        .received_uuid = received_uuid,
    };
}

btrfsbackup::config::Profile test_profile(const fs::path& root) {
    btrfsbackup::config::Profile profile{
        btrfsbackup::ProfileId{"default"},
        {
            btrfsbackup::config::LuksUuid{"11111111-2222-3333-4444-555555555555"},
            btrfsbackup::config::BtrfsUuid{"22222222-3333-4444-5555-666666666666"},
            btrfsbackup::config::PartitionUuid{""},
            btrfsbackup::config::MapperName{"backup"},
        },
        {
            btrfsbackup::config::RemoteSnapshotRoot{(root / "target" / "default" / "snapshots").string()},
            btrfsbackup::config::IncomingRoot{(root / "target" / "default" / ".incoming").string()},
        },
    };
    profile.name = "Default backup";
    profile.enabled = true;
    profile.target.device = btrfsbackup::config::TargetDevicePath{
        "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"
    };
    profile.target.serial = "";
    profile.target.mount_point = btrfsbackup::config::TargetMountPoint{root / "target" / "default"};
    profile.settings.incremental_required = false;
    profile.settings.keep_failed_local_snapshot = false;
    profile.settings.remote_retention = btrfsbackup::config::RetentionCount{2};
    profile.settings.local_retention = btrfsbackup::config::RetentionCount{2};
    btrfsbackup::config::ProfileSource source{btrfsbackup::SourceId{"root"}};
    source.name = "System";
    source.subvolume = btrfsbackup::config::SourceSubvolumePath{root / "source" / "root"};
    source.local_snapshot_dir = btrfsbackup::config::LocalSnapshotRoot{root / "source" / ".snapshots" / "root"};
    source.remote_retention = btrfsbackup::config::RetentionCount{2};
    source.local_retention = btrfsbackup::config::RetentionCount{2};
    profile.sources = {std::move(source)};
    return profile;
}

btrfsbackup::config::ApplicationPaths test_application_paths(const fs::path& root) {
    return {
        .state_root = root / "state",
        .status_root = root / "status",
        .history_root = root / "history",
        .target_mount_root = root / "target",
    };
}

void add_home_source(btrfsbackup::config::Profile& profile, const fs::path& root) {
    btrfsbackup::config::ProfileSource source{btrfsbackup::SourceId{"home"}};
    source.name = "Home";
    source.subvolume = btrfsbackup::config::SourceSubvolumePath{root / "source" / "home"};
    source.local_snapshot_dir = btrfsbackup::config::LocalSnapshotRoot{root / "source" / ".snapshots" / "home"};
    source.remote_retention = btrfsbackup::config::RetentionCount{2};
    source.local_retention = btrfsbackup::config::RetentionCount{2};
    profile.sources.push_back(std::move(source));
}

void write_profile(const fs::path& config_root, const btrfsbackup::config::Profile& profile) {
    fs::path config_path = config_root / "btrfs-backup.conf";
    if (!fs::exists(config_path)) {
        test_helpers::write_file(
            config_path,
            "CONFIG_VERSION=1\nTARGET_MOUNT_ROOT=" + fs::path(profile.target.mount_point).parent_path().string() + "\n"
        );
        chmod(config_path.c_str(), 0600);
    }
    fs::path profile_path = config_root / "profiles" / profile.id.value() / "profile.json";
    test_helpers::write_file(profile_path, btrfsbackup::config::profile_to_json(profile).dump(2));
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

void write_mountinfo(const fs::path& path, const btrfsbackup::config::Profile& profile) {
    std::string content;
    int mount_id = 21;
    for (const btrfsbackup::config::ProfileSource& source : profile.sources) {
        content += std::to_string(mount_id++) + " 31 0:20 / " + fs::path(source.subvolume).string() + " rw,relatime - btrfs /dev/source rw\n";
        content += std::to_string(mount_id++) + " 31 0:20 / " + fs::path(source.local_snapshot_dir).string() + " rw,relatime - btrfs /dev/source rw\n";
    }
    content += std::to_string(mount_id) + " 31 0:21 / " + fs::path(profile.target.mount_point).string() + " rw,relatime,nodev,nosuid,noexec,nosymfollow - btrfs /dev/mapper/backup rw\n";
    test_helpers::write_file(path, content);
}

class FixedClock final : public btrfsbackup::backup::IClock {
  public:
    btrfsbackup::RuntimeTimePoint timestamp = *btrfsbackup::parse_utc_timestamp("2026-08-23T080000Z");
    btrfsbackup::LocalDate today = *btrfsbackup::parse_local_date("2026-08-23");

    btrfsbackup::RuntimeTimePoint now() const override {
        return timestamp;
    }
    btrfsbackup::LocalDate local_date() const override {
        return today;
    }
};

class FixedRunIdGenerator final : public btrfsbackup::backup::IRunIdGenerator {
  public:
    btrfsbackup::RunId run_id{"20260823T080000Z-shadow"};
    btrfsbackup::RunId generate(btrfsbackup::RuntimeTimePoint) override {
        return run_id;
    }
};

struct ServiceFixture {
    btrfsbackup::backup::IBackupRunActionHandler& action_handler;
    btrfsbackup::backup::transfer::ITransferPipeline& transfer_pipeline;
    fs::path lock_root;
    btrfsbackup::config::ApplicationConfig application_config;
    btrfsbackup::backup::SnapshotMetadataReader snapshot_metadata_reader = nullptr;
};

std::string option_value(const std::vector<std::string>& args, const std::string& option, std::string fallback = {}) {
    for (std::size_t index = 0; index + 1 < args.size(); ++index) {
        if (args[index] == option) {
            return args[index + 1];
        }
    }
    return fallback;
}

std::string mount_uuid_value(
    const std::vector<std::string>& args,
    const std::string& source,
    std::string fallback
) {
    for (std::size_t index = 0; index + 2 < args.size(); ++index) {
        if (args[index] == "--mount-uuid" && args[index + 1] == source) {
            return args[index + 2];
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
        return btrfsbackup::cli::runner(config_root, args, output);
    }
    const btrfsbackup::ProfileId profile_id{option_value(args, "--profile", "default")};
    const btrfsbackup::config::Profile profile = btrfsbackup::platform::linux::load_profile_by_id(
        config_root,
        std::string(profile_id.value())
    );
    const fs::path mountinfo = option_value(args, "--mountinfo", "/proc/self/mountinfo");
    btrfsbackup::platform::linux::FileProfileRepository profiles(config_root, fixture->application_config);
    btrfsbackup::platform::linux::LinuxMountInspector mounts(mountinfo, [&args, target_uuid = profile.target.btrfs_uuid.value()](const std::string& source) {
        return mount_uuid_value(
            args,
            source,
            source.find("/dev/mapper/") == 0 ? target_uuid : "source-btrfs-uuid"
        );
    });
    btrfsbackup::platform::linux::PosixCommandRunner commands;
    btrfsbackup::platform::linux::SystemdTargetManager target_mounter(mounts, commands);
    btrfsbackup::backup::BackupPreflight preflight(mounts, target_mounter);
    test_support::FakeSafeDirectoryRootFactory safe_directories;
    btrfsbackup::platform::linux::PosixDurableFileOperations durable_files;
    btrfsbackup::state::FilePendingMarkerStore pending_markers(durable_files);
    btrfsbackup::backup::BackupDiscovery discovery(
        fixture->snapshot_metadata_reader
            ? fixture->snapshot_metadata_reader
            : btrfsbackup::backup::SnapshotMetadataReader{[](const fs::path&) {
                  return std::optional<btrfsbackup::backup::SnapshotMetadata>{};
              }},
        pending_markers,
        safe_directories
    );
    btrfsbackup::backup::BackupPlanBuilder plan_builder;
    DelegatingActionHandlerFactory action_handlers(fixture->action_handler);
    btrfsbackup::backup::DefaultBackupRunFactory run_factory(
        action_handlers,
        fixture->transfer_pipeline,
        safe_directories
    );
    btrfsbackup::platform::linux::FileBackupRunLeaseProvider leases(fixture->lock_root);
    btrfsbackup::state::FileRunStateRepository state(fixture->application_config.paths(), durable_files);
    btrfsbackup::state::FileCancellationMonitor file_cancellation_monitor(state);
    FixedClock clock;
    const std::string timestamp = option_value(args, "--timestamp", btrfsbackup::format_utc_snapshot_timestamp(clock.timestamp));
    const std::string today = option_value(args, "--today", btrfsbackup::format_local_date(clock.today));
    clock.timestamp = *btrfsbackup::parse_utc_timestamp(timestamp);
    clock.today = *btrfsbackup::parse_local_date(today);
    FixedRunIdGenerator run_ids;
    run_ids.run_id = btrfsbackup::RunId{option_value(
        args,
        "--run-id",
        compact_test_timestamp(timestamp) + "-shadow"
    )};
    btrfsbackup::CancellationToken owned_cancellation;
    btrfsbackup::CancellationToken& cancellation = external_cancellation == nullptr
        ? owned_cancellation
        : *external_cancellation;
    btrfsbackup::backup::LinkedCancellationMonitor cancellation_monitor(
        file_cancellation_monitor,
        cancellation
    );
    btrfsbackup::backup::BackupService service(
        profiles,
        fixture->application_config.paths(),
        preflight,
        discovery,
        plan_builder,
        run_factory,
        leases,
        state,
        state,
        state,
        state,
        cancellation_monitor,
        clock,
        run_ids
    );
    return btrfsbackup::cli::runner(args, output, service);
}

int run_runner(const fs::path& config_root, const std::vector<std::string>& args, std::ostream& output) {
    return btrfsbackup::cli::runner(config_root, args, output);
}

std::string profile_fingerprint(const fs::path& config_root, const btrfsbackup::config::Profile& profile) {
    return btrfsbackup::config::compute_config_fingerprint(
        std::string(btrfsbackup::config::current_configuration_fingerprint_version),
        config_root / "profiles" / profile.id.value() / "profile.json",
        {}
    );
}

void write_matching_last_success(const fs::path& config_root, const btrfsbackup::config::Profile& profile) {
    btrfsbackup::platform::linux::PosixDurableFileOperations durable_files;
    btrfsbackup::state::write_success_state(
        durable_files,
        config_root.parent_path() / "state" / "profiles" / profile.id.value(),
        btrfsbackup::state::SuccessState{
            .date = "2026-08-23",
            .timestamp = "2026-08-23T08:00:00+02:00",
            .run_id = "20260823T080000Z-previous",
            .profile_id = std::string(profile.id.value()),
            .profile_name = profile.name,
            .source_count = 1,
            .target_luks_uuid = profile.target.luks_uuid.value(),
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

    btrfsbackup::config::Profile profile = test_profile(root);
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
    };

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
            profile.target.btrfs_uuid.value(),
        },
        output,
        &services
    );

    btrfsbackup::config::Json json = btrfsbackup::config::Json::parse(output.str());
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

    btrfsbackup::config::Profile profile = test_profile(root);
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
    };

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
                                                                          output,
                                                                          &services
                                                                      ); }, "Btrfs UUID mismatch");

    fs::remove_all(root);
}

void test_runner_execute_rejects_busy_profile_before_target_access() {
    fs::path root = test_helpers::test_root("runner-command", "execute-profile-busy");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::config::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    fs::path lock_root = root / "locks";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    btrfsbackup::platform::linux::FileLock active_profile_lock(
        btrfsbackup::platform::linux::profile_lock_path(lock_root, std::string(profile.id.value()))
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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

    btrfsbackup::config::Json json = btrfsbackup::config::Json::parse(output.str());
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

    btrfsbackup::config::Profile active_profile = test_profile(root);
    btrfsbackup::config::Profile shared_target_profile = test_profile(root);
    shared_target_profile.id = btrfsbackup::ProfileId{"shared"};
    shared_target_profile.name = "Shared target";
    shared_target_profile.target.mount_point = btrfsbackup::config::TargetMountPoint{root / "target" / "shared"};
    shared_target_profile.paths.remote_root = btrfsbackup::config::RemoteSnapshotRoot{(root / "target" / "shared" / "snapshots").string()};
    shared_target_profile.paths.incoming_root = btrfsbackup::config::IncomingRoot{(root / "target" / "shared" / ".incoming").string()};
    btrfsbackup::config::Profile other_target_profile = test_profile(root);
    other_target_profile.id = btrfsbackup::ProfileId{"other"};
    other_target_profile.name = "Other target";
    other_target_profile.target.luks_uuid =
        btrfsbackup::config::LuksUuid{"33333333-4444-5555-6666-777777777777"};
    other_target_profile.target.btrfs_uuid =
        btrfsbackup::config::BtrfsUuid{"44444444-5555-6666-7777-888888888888"};
    other_target_profile.target.mount_point = btrfsbackup::config::TargetMountPoint{root / "target" / "other"};
    other_target_profile.paths.remote_root = btrfsbackup::config::RemoteSnapshotRoot{(root / "target" / "other" / "snapshots").string()};
    other_target_profile.paths.incoming_root = btrfsbackup::config::IncomingRoot{(root / "target" / "other" / ".incoming").string()};
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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
                    active_profile.target.btrfs_uuid.value(),
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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

    btrfsbackup::config::Json same_profile_json = btrfsbackup::config::Json::parse(same_profile_output.str());
    test_helpers::expect_eq("concurrent profile result", std::to_string(same_profile_result), "1");
    test_helpers::expect_eq(
        "concurrent profile error",
        same_profile_json.at("errorCode").get<std::string>(),
        "runner.profile_busy"
    );
    test_helpers::expect_true("concurrent profile effects", same_profile_action_handler.calls.empty(), "second runner must not execute actions");
    test_helpers::expect_true("concurrent profile transfers", same_profile_transfer_pipeline.plans.empty(), "second runner must not transfer");
    btrfsbackup::config::Json active_status = btrfsbackup::config::load_json_file(
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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

    btrfsbackup::config::Json shared_json = btrfsbackup::config::Json::parse(shared_output.str());
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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
            other_target_profile.target.btrfs_uuid.value(),
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

    btrfsbackup::config::Profile profile = test_profile(root);
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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
            profile.target.btrfs_uuid.value(),
        },
        output,
        &services
    );

    btrfsbackup::config::Json json = btrfsbackup::config::Json::parse(output.str());
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
        btrfsbackup::config::load_json_file(current).at("state") == "succeeded",
        "current status should be succeeded"
    );
    test_helpers::expect_true(
        "last success matches",
        btrfsbackup::state::last_success_matches(
            root / "state" / "profiles" / "default",
            "2026-08-23",
            profile.target.luks_uuid.value(),
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

    btrfsbackup::config::Profile profile = test_profile(root);
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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
            profile.target.btrfs_uuid.value(),
        },
        output,
        &services
    );

    btrfsbackup::config::Json json = btrfsbackup::config::Json::parse(output.str());
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
        btrfsbackup::config::load_json_file(current).at("state") == "skipped",
        "current status should be skipped"
    );
    test_helpers::expect_true(
        "daily skip history",
        btrfsbackup::config::load_json_file(history).at("state") == "skipped",
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

    btrfsbackup::config::Profile profile = test_profile(root);
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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
            profile.target.btrfs_uuid.value(),
        },
        output,
        &services
    );

    btrfsbackup::config::Json json = btrfsbackup::config::Json::parse(output.str());
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

    btrfsbackup::config::Profile profile = test_profile(root);
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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
            profile.target.btrfs_uuid.value(),
        },
        output,
        &services
    );

    btrfsbackup::config::Json json = btrfsbackup::config::Json::parse(output.str());
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

    btrfsbackup::config::Profile profile = test_profile(root);
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
    };

    std::ostringstream output;
    const int result = run_runner(
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
            profile.target.btrfs_uuid.value(),
        },
        output,
        &services
    );
    const auto result_json = btrfsbackup::config::Json::parse(output.str());
    test_helpers::expect_eq("execute transfer failure exit", std::to_string(result), "1");
    test_helpers::expect_true("execute transfer failure completed", !result_json.at("completed").get<bool>(), "failed run must not complete");
    test_helpers::expect_contains("execute transfer failure message", result_json.at("errorMessage").get<std::string>(), "producer failed with exit code 7");

    fs::path checkpoint = root / "state" / "profiles" / "default" / "checkpoint.json";
    fs::path current = root / "status" / "default" / "current.json";
    fs::path history = root / "history" / "default" / "20260823T080000Z-123-456.json";
    btrfsbackup::config::Json current_json = btrfsbackup::config::load_json_file(current);
    test_helpers::expect_true("failed checkpoint exists", fs::is_regular_file(checkpoint), "missing checkpoint before failure");
    test_helpers::expect_true(
        "failed checkpoint action",
        btrfsbackup::config::load_json_file(checkpoint).at("action") == "create-snapshot",
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
        btrfsbackup::config::load_json_file(history).at("errorCode").get<std::string>(),
        "transfer.producer_failed"
    );
    test_helpers::expect_true(
        "failed history",
        btrfsbackup::config::load_json_file(history).at("state") == "failed",
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

    btrfsbackup::config::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionHandler action_handler;
    action_handler.should_throw = true;
    action_handler.throw_on = btrfsbackup::backup::BackupRunActionKind::CommitReceived;
    ConfigurableTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
    };

    std::ostringstream output;
    const int result = run_runner(
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
            profile.target.btrfs_uuid.value(),
        },
        output,
        &services
    );
    const auto result_json = btrfsbackup::config::Json::parse(output.str());
    test_helpers::expect_eq("execute commit failure exit", std::to_string(result), "1");
    test_helpers::expect_contains("execute commit failure message", result_json.at("errorMessage").get<std::string>(), "injected action failure");

    fs::path checkpoint = root / "state" / "profiles" / "default" / "checkpoint.json";
    fs::path current = root / "status" / "default" / "current.json";
    fs::path history = root / "history" / "default" / "20260823T080000Z-123-456.json";
    test_helpers::expect_true(
        "commit failure checkpoint action",
        btrfsbackup::config::load_json_file(checkpoint).at("action") == "verify-received",
        "failed commit should not be checkpointed"
    );
    test_helpers::expect_true(
        "commit failure current",
        btrfsbackup::config::load_json_file(current).at("state") == "failed",
        "current status should fail"
    );
    test_helpers::expect_true(
        "commit failure phase",
        btrfsbackup::config::load_json_file(history).at("phase") == "commit-received",
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

    btrfsbackup::config::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionHandler action_handler;
    action_handler.should_throw = true;
    action_handler.throw_on = btrfsbackup::backup::BackupRunActionKind::VerifyReceived;
    ConfigurableTransferPipeline transfer_pipeline;
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
    };

    std::ostringstream output;
    const int result = run_runner(
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
            profile.target.btrfs_uuid.value(),
        },
        output,
        &services
    );
    const auto result_json = btrfsbackup::config::Json::parse(output.str());
    test_helpers::expect_eq("execute verify failure exit", std::to_string(result), "1");
    test_helpers::expect_contains("execute verify failure message", result_json.at("errorMessage").get<std::string>(), "injected action failure");

    fs::path checkpoint = root / "state" / "profiles" / "default" / "checkpoint.json";
    fs::path current = root / "status" / "default" / "current.json";
    fs::path history = root / "history" / "default" / "20260823T080000Z-123-456.json";
    test_helpers::expect_true(
        "verify failure checkpoint action",
        btrfsbackup::config::load_json_file(checkpoint).at("action") == "send-receive",
        "failed verify should not be checkpointed"
    );
    test_helpers::expect_true(
        "verify failure current",
        btrfsbackup::config::load_json_file(current).at("state") == "failed",
        "current status should fail"
    );
    test_helpers::expect_true(
        "verify failure phase",
        btrfsbackup::config::load_json_file(history).at("phase") == "verify-received",
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

    btrfsbackup::config::Profile profile = test_profile(root);
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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
            profile.target.btrfs_uuid.value(),
        },
        output,
        &services
    );

    btrfsbackup::config::Json json = btrfsbackup::config::Json::parse(output.str());
    test_helpers::expect_eq("multi execute result", std::to_string(result), "0");
    test_helpers::expect_true("multi completed", json.at("completed").get<bool>(), "run should complete");
    test_helpers::expect_eq("multi transfers", std::to_string(transfer_pipeline.plans.size()), "2");
    test_helpers::expect_true("root actions", std::find(action_handler.calls.begin(), action_handler.calls.end(), "root:" + action_name(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot)) != action_handler.calls.end(), "missing root create action");
    test_helpers::expect_true("home actions", std::find(action_handler.calls.begin(), action_handler.calls.end(), "home:" + action_name(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot)) != action_handler.calls.end(), "missing home create action");

    btrfsbackup::config::Json current = btrfsbackup::config::load_json_file(root / "status" / "default" / "current.json");
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

    btrfsbackup::config::Profile profile = test_profile(root);
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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
            profile.target.btrfs_uuid.value(),
        },
        output,
        &services
    );

    test_helpers::expect_eq("incremental result", std::to_string(result), "0");
    btrfsbackup::config::Json json = btrfsbackup::config::Json::parse(output.str());
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

    btrfsbackup::config::Profile profile = test_profile(root);
    profile.sources.at(0).local_retention = btrfsbackup::config::RetentionCount{2};
    profile.sources.at(0).remote_retention = btrfsbackup::config::RetentionCount{2};
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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
            profile.target.btrfs_uuid.value(),
        },
        output,
        &services
    );

    test_helpers::expect_eq("retention result", std::to_string(result), "0");
    test_helpers::expect_true("remote retention action", std::find(action_handler.calls.begin(), action_handler.calls.end(), "root:" + action_name(btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention)) != action_handler.calls.end(), "missing remote retention action");
    test_helpers::expect_true("local retention action", std::find(action_handler.calls.begin(), action_handler.calls.end(), "root:" + action_name(btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention)) != action_handler.calls.end(), "missing local retention action");
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

    btrfsbackup::config::Profile profile = test_profile(root);
    fs::path pending = root / "source" / ".snapshots" / "root" / "root-2026-08-22T080000Z";
    fs::create_directories(pending);

    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);
    btrfsbackup::platform::linux::PosixDurableFileOperations durable_files;
    btrfsbackup::state::write_pending_marker(
        durable_files,
        root / "state" / "profiles" / "default",
        btrfsbackup::backup::PendingMarker{
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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
            profile.target.btrfs_uuid.value(),
        },
        output,
        &services
    );

    test_helpers::expect_eq("pending recovery result", std::to_string(result), "0");
    test_helpers::expect_true("recover action first", !action_handler.calls.empty(), "expected action calls");
    test_helpers::expect_eq(
        "recover first action",
        action_handler.calls.at(0),
        "root:" + action_name(btrfsbackup::backup::BackupRunActionKind::RecoverPending)
    );
    test_helpers::expect_eq("pending delete count", std::to_string(action_handler.recovered_pending_paths.size()), "1");
    test_helpers::expect_eq("pending delete path", action_handler.recovered_pending_paths.at(0), pending.string());

    fs::remove_all(root);
}

void test_runner_cancel_validates_active_run_identity_without_target_mount() {
    fs::path root = test_helpers::test_root("runner-command", "cancel");
    btrfsbackup::config::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    write_profile(config_root, profile);
    write_application_config(config_root, root);

    RecordingActionHandler action_handler;
    ConfigurableTransferPipeline transfer_pipeline;
    const fs::path lock_root = root / "locks";
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = lock_root,
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
    };
    btrfsbackup::platform::linux::FileLock active_profile_lock(
        btrfsbackup::platform::linux::profile_lock_path(lock_root, "default")
    );
    test_helpers::expect_true(
        "active profile lock",
        active_profile_lock.try_acquire(),
        "cannot simulate an active run"
    );

    fs::path profile_state_dir = root / "state" / "profiles" / "default";
    btrfsbackup::platform::linux::PosixDurableFileOperations durable_files;
    btrfsbackup::state::write_active_run(
        durable_files,
        profile_state_dir,
        btrfsbackup::RunId{"active-run"}
    );

    std::ostringstream output;
    int result = run_runner(
        config_root,
        {
            "cancel",
            "--profile",
            "default",
            "--run-id",
            "active-run",
        },
        output,
        &services
    );

    btrfsbackup::config::Json json = btrfsbackup::config::Json::parse(output.str());
    test_helpers::expect_eq("cancel result", std::to_string(result), "0");
    test_helpers::expect_eq("cancel mode", json.at("mode").get<std::string>(), "cpp-cancel");
    test_helpers::expect_true("cancel requested json", json.at("cancelRequested").get<bool>(), "cancel should be requested");
    test_helpers::expect_true(
        "cancel request exists",
        btrfsbackup::state::cancel_requested(profile_state_dir),
        "cancel request file missing"
    );
    test_helpers::expect_eq("cancel run id", json.at("runId").get<std::string>(), "active-run");

    std::ostringstream mismatch_output;
    int mismatch_result = run_runner(
        config_root,
        {
            "cancel",
            "--profile",
            "default",
            "--run-id",
            "other-run",
        },
        mismatch_output,
        &services
    );
    btrfsbackup::config::Json mismatch_json = btrfsbackup::config::Json::parse(mismatch_output.str());
    test_helpers::expect_eq("mismatch result", std::to_string(mismatch_result), "1");
    test_helpers::expect_true(
        "mismatch rejected",
        !mismatch_json.at("cancelRequested").get<bool>(),
        "mismatched run was cancelled"
    );
    test_helpers::expect_eq(
        "mismatch error code",
        mismatch_json.at("errorCode").get<std::string>(),
        "runner.run_mismatch"
    );
    test_helpers::expect_true(
        "accepted marker preserved",
        btrfsbackup::state::cancel_requested(profile_state_dir, btrfsbackup::RunId{"active-run"}),
        "mismatched request replaced the accepted marker"
    );

    active_profile_lock.release();
    std::ostringstream stale_output;
    int stale_result = run_runner(
        config_root,
        {
            "cancel",
            "--profile",
            "default",
            "--run-id",
            "active-run",
        },
        stale_output,
        &services
    );
    btrfsbackup::config::Json stale_json = btrfsbackup::config::Json::parse(stale_output.str());
    test_helpers::expect_eq("stale result", std::to_string(stale_result), "1");
    test_helpers::expect_true(
        "stale rejected",
        !stale_json.at("cancelRequested").get<bool>(),
        "completed run was cancelled"
    );
    test_helpers::expect_eq(
        "stale error code",
        stale_json.at("errorCode").get<std::string>(),
        "runner.stale_run"
    );

    fs::remove_all(root);
}

void test_runner_execute_honors_cancel_request_during_transfer() {
    fs::path root = test_helpers::test_root("runner-command", "execute-cancel");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "default" / "snapshots" / "root");
    fs::create_directories(root / "target" / "default" / ".incoming");

    btrfsbackup::config::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionHandler action_handler;
    ConfigurableTransferPipeline transfer_pipeline;
    fs::path profile_state_dir = root / "state" / "profiles" / "default";
    transfer_pipeline.request_cancel_path = profile_state_dir;
    transfer_pipeline.request_cancel_run_id = btrfsbackup::RunId{"20260823T080000Z-123-456"};
    ServiceFixture services{
        .action_handler = action_handler,
        .transfer_pipeline = transfer_pipeline,
        .lock_root = root / "locks",
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
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
            profile.target.btrfs_uuid.value(),
        },
        output,
        &services
    );

    btrfsbackup::config::Json json = btrfsbackup::config::Json::parse(output.str());
    test_helpers::expect_eq("execute cancel result", std::to_string(result), "1");
    test_helpers::expect_true("execute cancel incomplete", !json.at("completed").get<bool>(), "cancelled run should not complete");
    test_helpers::expect_true("execute cancel flag", json.at("cancelled").get<bool>(), "cancelled flag should be true");

    fs::path current = root / "status" / "default" / "current.json";
    btrfsbackup::config::Json current_json = btrfsbackup::config::load_json_file(current);
    test_helpers::expect_eq("execute cancel status state", current_json.at("state").get<std::string>(), "cancelled");
    test_helpers::expect_eq("execute cancel code", current_json.at("errorCode").get<std::string>(), "backup.cancelled");
    test_helpers::expect_true(
        "execute cancel request cleaned",
        !btrfsbackup::state::cancel_requested(profile_state_dir),
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

    btrfsbackup::config::Profile profile = test_profile(root);
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
        .application_config = btrfsbackup::config::ApplicationConfig(test_application_paths(root)),
    };
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::cli::TerminationSignalMonitor termination_signals(cancellation);
    std::ostringstream output;
    int result = -1;
    std::exception_ptr runner_error;
    std::thread runner_thread([&] {
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
                    profile.target.btrfs_uuid.value(),
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
    runner_thread.join();
    if (runner_error != nullptr) {
        std::rethrow_exception(runner_error);
    }

    btrfsbackup::config::Json run = btrfsbackup::config::Json::parse(output.str());
    btrfsbackup::config::Json current = btrfsbackup::config::load_json_file(root / "status" / "default" / "current.json");
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
    test_runner_cancel_validates_active_run_identity_without_target_mount();
    test_runner_execute_honors_cancel_request_during_transfer();
    test_runner_execute_handles_sigint_as_cancelled_with_recovery_marker();

    return test_helpers::finish("runner command tests");
}
