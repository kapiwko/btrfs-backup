// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/runner_command.hpp>

#include <filesystem>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include <backup/action_handlers/backup_run_action_handler.hpp>
#include <backup/action_handlers/hook_action_handler.hpp>
#include <backup/action_handlers/recovery_action_handler.hpp>
#include <backup/action_handlers/repository_action_handler.hpp>
#include <backup/action_handlers/retention_action_handler.hpp>
#include <backup/action_handlers/snapshot_action_handler.hpp>
#include <backup/backup_discovery.hpp>
#include <backup/backup_plan_builder.hpp>
#include <backup/backup_preflight.hpp>
#include <backup/backup_run.hpp>
#include <backup/linked_cancellation_monitor.hpp>
#include <backup/system_run_context.hpp>
#include <backup/transfer/async_transfer.hpp>
#include <cli/runner_options.hpp>
#include <cli/runner_presenter.hpp>
#include <core/runtime_time.hpp>
#include <platform/linux/btrfs_util_operations.hpp>
#include <platform/linux/config/application_config.hpp>
#include <platform/linux/config/profile_repository.hpp>
#include <platform/linux/config/profile_runtime_policy.hpp>
#include <platform/linux/file_backup_run_lease_provider.hpp>
#include <platform/linux/file_lock.hpp>
#include <platform/linux/mount_info.hpp>
#include <platform/linux/posix_command_runner.hpp>
#include <platform/linux/posix_durable_file_operations.hpp>
#include <platform/linux/posix_filesystem.hpp>
#include <platform/linux/posix_transfer_pipeline.hpp>
#include <platform/linux/safe_directory_root.hpp>
#include <platform/linux/systemd_target_manager.hpp>
#include <platform/linux/trusted_executable.hpp>
#include <state/file_pending_marker_store.hpp>
#include <state/file_run_state_repository.hpp>

namespace fs = std::filesystem;

namespace {

class CommandClock final : public btrfsbackup::backup::IClock {
  public:
    CommandClock(btrfsbackup::RuntimeTimePoint timestamp, btrfsbackup::LocalDate today)
        : timestamp_(timestamp), today_(today) {
    }

    btrfsbackup::RuntimeTimePoint now() const override {
        return timestamp_;
    }
    btrfsbackup::LocalDate local_date() const override {
        return today_;
    }

  private:
    btrfsbackup::RuntimeTimePoint timestamp_;
    btrfsbackup::LocalDate today_;
};

class CommandRunIdGenerator final : public btrfsbackup::backup::IRunIdGenerator {
  public:
    explicit CommandRunIdGenerator(btrfsbackup::RunId run_id) : run_id_(std::move(run_id)) {
    }

    btrfsbackup::RunId generate(btrfsbackup::RuntimeTimePoint) override {
        return run_id_;
    }

  private:
    btrfsbackup::RunId run_id_;
};

class PosixBackupRunFactory final : public btrfsbackup::backup::IBackupRunFactory {
  public:
    PosixBackupRunFactory(
        btrfsbackup::backup::IBtrfsOperations& btrfs,
        btrfsbackup::backup::IFileSystem& filesystem,
        btrfsbackup::backup::ICommandRunner& commands,
        btrfsbackup::backup::transfer::ITransferPipeline& transfers,
        btrfsbackup::backup::IPendingMarkerStore& pending_markers,
        const btrfsbackup::backup::ISafeDirectoryRootFactory& safe_directories
    )
        : btrfs_(btrfs),
          filesystem_(filesystem),
          commands_(commands),
          transfers_(transfers),
          pending_markers_(pending_markers),
          safe_directories_(safe_directories) {
    }

    btrfsbackup::backup::BackupRunExecutionResult execute(
        btrfsbackup::backup::BackupRunPlan plan,
        btrfsbackup::backup::IBackupRunEventSink& events,
        btrfsbackup::backup::IBackupRunCheckpointStore& checkpoints,
        btrfsbackup::CancellationToken& cancellation
    ) override {
        btrfsbackup::backup::SnapshotActionHandler snapshots(
            btrfs_,
            filesystem_,
            pending_markers_,
            std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/")
        );
        btrfsbackup::backup::RecoveryActionHandler recovery(
            btrfs_,
            pending_markers_,
            std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/"),
            std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>(plan.target_mount_point)
        );
        btrfsbackup::backup::RetentionActionHandler retention(
            btrfs_,
            std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/"),
            std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>(plan.target_mount_point)
        );
        btrfsbackup::platform::linux::PosixTrustedExecutableResolver hook_executables(
            btrfsbackup::platform::linux::trusted_hook_directory
        );
        btrfsbackup::backup::HookActionHandler hooks(commands_, hook_executables);
        btrfsbackup::platform::linux::SafeDirectoryRoot local_repository_root("/");
        btrfsbackup::platform::linux::SafeDirectoryRoot target_repository_root(plan.target_mount_point);
        btrfsbackup::backup::RepositoryActionHandler repository(
            btrfs_,
            pending_markers_,
            local_repository_root,
            target_repository_root
        );
        btrfsbackup::backup::BackupRunActionHandler action_handler(
            snapshots,
            recovery,
            retention,
            hooks,
            repository
        );
        btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers_);
        btrfsbackup::backup::BackupRun run(
            std::move(plan),
            action_handler,
            async_transfers,
            checkpoints,
            safe_directories_
        );
        return run.execute(events, cancellation);
    }

  private:
    btrfsbackup::backup::IBtrfsOperations& btrfs_;
    btrfsbackup::backup::IFileSystem& filesystem_;
    btrfsbackup::backup::ICommandRunner& commands_;
    btrfsbackup::backup::transfer::ITransferPipeline& transfers_;
    btrfsbackup::backup::IPendingMarkerStore& pending_markers_;
    const btrfsbackup::backup::ISafeDirectoryRootFactory& safe_directories_;
};

class ProductionBackupComposition {
  public:
    ProductionBackupComposition(
        const fs::path& config_root,
        const btrfsbackup::cli::RunnerOptions& options,
        btrfsbackup::CancellationToken& cancellation
    )
        : config_(btrfsbackup::platform::linux::load_application_config(config_root)),
          profiles_(config_root, config_),
          mounts_(options.mountinfo, [&options](const std::string& source) {
              const auto found = options.mount_uuid_overrides.find(source);
              return found == options.mount_uuid_overrides.end()
                  ? btrfsbackup::platform::linux::blkid_filesystem_uuid(source)
                  : found->second;
          }),
          target_mounter_(mounts_, commands_), preflight_(mounts_, target_mounter_), pending_markers_(durable_files_), discovery_(btrfsbackup::platform::linux::read_btrfs_snapshot_metadata, pending_markers_, safe_directories_), run_factory_(btrfs_, filesystem_, commands_, transfers_, pending_markers_, safe_directories_), leases_(btrfsbackup::platform::linux::default_lock_root()), state_(config_.paths(), durable_files_), file_cancellation_monitor_(state_), cancellation_monitor_(file_cancellation_monitor_, cancellation), clock_(options.timestamp, options.today), run_ids_(options.run_id), service_(profiles_, config_.paths(), preflight_, discovery_, plan_builder_, run_factory_, leases_, state_, state_, state_, state_, cancellation_monitor_, clock_, run_ids_) {
    }

    btrfsbackup::backup::BackupService& service() {
        return service_;
    }

  private:
    btrfsbackup::config::ApplicationConfig config_;
    btrfsbackup::platform::linux::FileProfileRepository profiles_;
    btrfsbackup::platform::linux::LinuxMountInspector mounts_;
    btrfsbackup::platform::linux::PosixCommandRunner commands_;
    btrfsbackup::platform::linux::SystemdTargetManager target_mounter_;
    btrfsbackup::backup::BackupPreflight preflight_;
    btrfsbackup::platform::linux::SafeDirectoryRootFactory safe_directories_;
    btrfsbackup::platform::linux::LibBtrfsOperations btrfs_;
    btrfsbackup::platform::linux::PosixFileSystem filesystem_;
    btrfsbackup::platform::linux::PosixTransferPipeline transfers_;
    btrfsbackup::platform::linux::PosixDurableFileOperations durable_files_;
    btrfsbackup::state::FilePendingMarkerStore pending_markers_;
    btrfsbackup::backup::BackupDiscovery discovery_;
    btrfsbackup::backup::BackupPlanBuilder plan_builder_;
    PosixBackupRunFactory run_factory_;
    btrfsbackup::platform::linux::FileBackupRunLeaseProvider leases_;
    btrfsbackup::state::FileRunStateRepository state_;
    btrfsbackup::state::FileCancellationMonitor file_cancellation_monitor_;
    btrfsbackup::backup::LinkedCancellationMonitor cancellation_monitor_;
    CommandClock clock_;
    CommandRunIdGenerator run_ids_;
    btrfsbackup::backup::BackupService service_;
};

int run_with_service(
    const btrfsbackup::cli::RunnerOptions& options,
    std::ostream& output,
    btrfsbackup::backup::BackupService& service
) {
    if (options.command == btrfsbackup::cli::RunnerCommandKind::Cancel) {
        return btrfsbackup::cli::present_runner_cancellation(
            service.cancel({options.request.profile_id, options.run_id}),
            output
        );
    }
    if (options.command == btrfsbackup::cli::RunnerCommandKind::Plan) {
        return btrfsbackup::cli::present_runner_plan(service.plan(options.request), output);
    }
    return btrfsbackup::cli::present_runner_execution(service.start(options.request), output);
}

bool present_help(const std::vector<std::string>& args, std::ostream& output, int& result) {
    if (args.empty()) {
        btrfsbackup::cli::print_runner_usage(output);
        result = 2;
        return true;
    }
    if (args.front() == "-h" || args.front() == "--help") {
        btrfsbackup::cli::print_runner_usage(output);
        result = 0;
        return true;
    }
    return false;
}

} // namespace

namespace btrfsbackup::cli {

int runner(
    const std::vector<std::string>& args,
    std::ostream& output,
    btrfsbackup::backup::BackupService& service
) {
    int early_result = 0;
    if (present_help(args, output, early_result)) {
        return early_result;
    }
    return run_with_service(parse_runner_options(args), output, service);
}

int runner(const fs::path& profile_config_dir, const std::vector<std::string>& args, std::ostream& output) {
    CancellationToken cancellation;
    return runner(profile_config_dir, args, output, cancellation);
}

int runner(
    const fs::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    CancellationToken& cancellation
) {
    int early_result = 0;
    if (present_help(args, output, early_result)) {
        return early_result;
    }
    const RunnerOptions options = parse_runner_options(args);
    ProductionBackupComposition composition(profile_config_dir, options, cancellation);
    return run_with_service(options, output, composition.service());
}

} // namespace btrfsbackup::cli
