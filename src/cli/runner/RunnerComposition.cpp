// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/runner/RunnerComposition.hpp>

#include <memory>
#include <string>
#include <utility>

#include <backup/planning/BackupDiscovery.hpp>
#include <backup/planning/BackupPlanBuilder.hpp>
#include <backup/planning/BackupPreflight.hpp>
#include <backup/BackupService.hpp>
#include <backup/execution/actions/DefaultBackupRunActionHandlerFactory.hpp>
#include <backup/execution/DefaultBackupRunFactory.hpp>
#include <backup/execution/LinkedCancellationMonitor.hpp>
#include <backup/execution/SystemRunContext.hpp>
#include <cli/runner/RunnerOptions.hpp>
#include <core/Cancellation.hpp>
#include <core/RuntimeTime.hpp>
#include <platform/linux/storage/LibBtrfsOperations.hpp>
#include <platform/linux/config/ApplicationConfig.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>
#include <platform/linux/config/ProfileRuntimePolicy.hpp>
#include <platform/linux/filesystem/FileBackupRunLeaseProvider.hpp>
#include <platform/linux/filesystem/FileLock.hpp>
#include <platform/linux/storage/MountInfo.hpp>
#include <platform/linux/process/PosixCommandRunner.hpp>
#include <platform/linux/filesystem/PosixDurableFileOperations.hpp>
#include <platform/linux/filesystem/PosixFileSystem.hpp>
#include <platform/linux/transfer/PosixTransferPipeline.hpp>
#include <platform/linux/filesystem/SafeDirectoryRoot.hpp>
#include <platform/linux/systemd/SystemdTargetManager.hpp>
#include <platform/linux/filesystem/TrustedExecutable.hpp>
#include <state/FilePendingMarkerStore.hpp>
#include <state/FileRunStateRepository.hpp>
#include <state/FileCancellationMonitor.hpp>

namespace btrfsbackup::cli::runner {
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
                  ? platform::linux::storage::blkid_filesystem_uuid(source)
                  : found->second;
          }),
          target_mounter(mounts, commands), preflight(mounts, target_mounter), pending_markers(durable_files), discovery(platform::linux::storage::read_btrfs_snapshot_metadata, pending_markers, safe_directories), hook_executables(platform::linux::trusted_hook_directory), clock(options.timestamp, options.today), action_handlers(btrfs, filesystem, commands, pending_markers, clock, safe_directories, hook_executables), run_factory(action_handlers, transfers, safe_directories), leases(platform::linux::filesystem::default_lock_root()), state(config.paths(), durable_files), file_cancellation_monitor(state), cancellation_monitor(file_cancellation_monitor, cancellation), run_ids(options.run_id), sessions(leases, state, state, state, cancellation_monitor), backup_service(profiles, config.paths(), preflight, discovery, plan_builder, run_factory, state, sessions, clock, run_ids) {
    }

    config::ApplicationConfig config;
    platform::linux::FileProfileRepository profiles;
    platform::linux::storage::LinuxMountInspector mounts;
    platform::linux::process::PosixCommandRunner commands;
    platform::linux::systemd::SystemdTargetManager target_mounter;
    backup::planning::BackupPreflight preflight;
    platform::linux::filesystem::SafeDirectoryRootFactory safe_directories;
    platform::linux::storage::LibBtrfsOperations btrfs;
    platform::linux::filesystem::PosixFileSystem filesystem;
    platform::linux::transfer::PosixTransferPipeline transfers;
    platform::linux::filesystem::PosixDurableFileOperations durable_files;
    state::FilePendingMarkerStore pending_markers;
    backup::planning::BackupDiscovery discovery;
    backup::planning::BackupPlanBuilder plan_builder;
    platform::linux::filesystem::PosixTrustedExecutableResolver hook_executables;
    ConfiguredRunnerClock clock;
    backup::execution::DefaultBackupRunActionHandlerFactory action_handlers;
    backup::execution::DefaultBackupRunFactory run_factory;
    platform::linux::filesystem::FileBackupRunLeaseProvider leases;
    state::FileRunStateRepository state;
    state::FileCancellationMonitor file_cancellation_monitor;
    backup::execution::LinkedCancellationMonitor cancellation_monitor;
    ConfiguredRunnerRunIdGenerator run_ids;
    backup::execution::RunSessionFactory sessions;
    backup::BackupService backup_service;
};

RunnerComposition::RunnerComposition(
    const std::filesystem::path& config_root,
    const RunnerOptions& options,
    CancellationToken& cancellation
)
    : impl_(std::make_unique<Impl>(config_root, options, cancellation)) {
}

RunnerComposition::~RunnerComposition() noexcept = default;

backup::BackupService& RunnerComposition::service() {
    return impl_->backup_service;
}

} // namespace btrfsbackup::cli::runner
