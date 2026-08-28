// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/runner_composition.hpp>

#include <memory>
#include <string>
#include <utility>

#include <backup/backup_discovery.hpp>
#include <backup/backup_plan_builder.hpp>
#include <backup/backup_preflight.hpp>
#include <backup/backup_service.hpp>
#include <backup/default_backup_run_action_handler_factory.hpp>
#include <backup/default_backup_run_factory.hpp>
#include <backup/linked_cancellation_monitor.hpp>
#include <backup/system_run_context.hpp>
#include <cli/runner_options.hpp>
#include <core/cancellation.hpp>
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

namespace btrfsbackup::cli {
namespace {

class ConfiguredRunnerClock final : public backup::IClock {
  public:
    ConfiguredRunnerClock(RuntimeTimePoint timestamp, LocalDate today)
        : timestamp_(timestamp), today_(today) {
    }

    RuntimeTimePoint now() const override {
        return timestamp_;
    }
    LocalDate local_date() const override {
        return today_;
    }

  private:
    RuntimeTimePoint timestamp_;
    LocalDate today_;
};

class ConfiguredRunnerRunIdGenerator final : public backup::IRunIdGenerator {
  public:
    explicit ConfiguredRunnerRunIdGenerator(RunId run_id) : run_id_(std::move(run_id)) {
    }

    RunId generate(RuntimeTimePoint) override {
        return run_id_;
    }

  private:
    RunId run_id_;
};

} // namespace

struct RunnerComposition::Impl {
    Impl(const std::filesystem::path& config_root, const RunnerOptions& options, CancellationToken& cancellation)
        : config(platform::linux::load_application_config(config_root)),
          profiles(config_root, config),
          mounts(options.mountinfo, [&options](const std::string& source) {
              const auto found = options.mount_uuid_overrides.find(source);
              return found == options.mount_uuid_overrides.end()
                  ? platform::linux::blkid_filesystem_uuid(source)
                  : found->second;
          }),
          target_mounter(mounts, commands), preflight(mounts, target_mounter), pending_markers(durable_files), discovery(platform::linux::read_btrfs_snapshot_metadata, pending_markers, safe_directories), hook_executables(platform::linux::trusted_hook_directory), action_handlers(btrfs, filesystem, commands, pending_markers, safe_directories, hook_executables), run_factory(action_handlers, transfers, safe_directories), leases(platform::linux::default_lock_root()), state(config.paths(), durable_files), file_cancellation_monitor(state), cancellation_monitor(file_cancellation_monitor, cancellation), clock(options.timestamp, options.today), run_ids(options.run_id), backup_service(profiles, config.paths(), preflight, discovery, plan_builder, run_factory, leases, state, state, state, state, cancellation_monitor, clock, run_ids) {
    }

    config::ApplicationConfig config;
    platform::linux::FileProfileRepository profiles;
    platform::linux::LinuxMountInspector mounts;
    platform::linux::PosixCommandRunner commands;
    platform::linux::SystemdTargetManager target_mounter;
    backup::BackupPreflight preflight;
    platform::linux::SafeDirectoryRootFactory safe_directories;
    platform::linux::LibBtrfsOperations btrfs;
    platform::linux::PosixFileSystem filesystem;
    platform::linux::PosixTransferPipeline transfers;
    platform::linux::PosixDurableFileOperations durable_files;
    state::FilePendingMarkerStore pending_markers;
    backup::BackupDiscovery discovery;
    backup::BackupPlanBuilder plan_builder;
    platform::linux::PosixTrustedExecutableResolver hook_executables;
    backup::DefaultBackupRunActionHandlerFactory action_handlers;
    backup::DefaultBackupRunFactory run_factory;
    platform::linux::FileBackupRunLeaseProvider leases;
    state::FileRunStateRepository state;
    state::FileCancellationMonitor file_cancellation_monitor;
    backup::LinkedCancellationMonitor cancellation_monitor;
    ConfiguredRunnerClock clock;
    ConfiguredRunnerRunIdGenerator run_ids;
    backup::BackupService backup_service;
};

RunnerComposition::RunnerComposition(
    const std::filesystem::path& config_root,
    const RunnerOptions& options,
    CancellationToken& cancellation
)
    : impl_(std::make_unique<Impl>(config_root, options, cancellation)) {
}

RunnerComposition::~RunnerComposition() = default;

backup::BackupService& RunnerComposition::service() {
    return impl_->backup_service;
}

} // namespace btrfsbackup::cli
