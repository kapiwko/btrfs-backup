// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/backup_run_action_handler.hpp>
#include <backup/backup_service.hpp>
#include <backup/ports/command_runner.hpp>
#include <backup/snapshot_inventory.hpp>
#include <backup/transfer/transfer_pipeline.hpp>
#include <config/application_config.hpp>

namespace btrfsbackup {

class FileProfileRepository final : public IProfileRepository {
  public:
    explicit FileProfileRepository(std::filesystem::path config_root);
    FileProfileRepository(std::filesystem::path config_root, ApplicationConfig application_config);

    [[nodiscard]] Profile get(const ProfileId& profile_id) const override;
    [[nodiscard]] const ApplicationPaths& application_paths() const override;
    [[nodiscard]] std::string fingerprint(const Profile& profile) const override;

  private:
    std::filesystem::path config_root_;
    ApplicationConfig application_config_;
};

class SystemdTargetMounter final : public ITargetMounter {
  public:
    SystemdTargetMounter(IMountInspector& mounts, ICommandRunner& commands);
    void ensure_mounted(const Profile& profile) override;

  private:
    IMountInspector& mounts_;
    ICommandRunner& commands_;
};

class DefaultBackupPlanner final : public IBackupPlanner {
  public:
    explicit DefaultBackupPlanner(SnapshotMetadataReader metadata_reader, bool secure_paths = true);

    [[nodiscard]] BackupRunPlan build(
        const Profile& profile,
        const std::vector<MountEntry>& mounts,
        const ApplicationPaths& paths,
        const RunId& run_id,
        const std::string& snapshot_timestamp
    ) const override;

  private:
    SnapshotMetadataReader metadata_reader_;
    bool secure_paths_;
};

class DefaultBackupRunFactory final : public IBackupRunFactory {
  public:
    DefaultBackupRunFactory(
        IBackupRunActionHandler& action_handler,
        ITransferPipeline& transfers,
        bool pin_transfer_paths = true
    );

    [[nodiscard]] BackupRunExecutionResult execute(
        BackupRunPlan plan,
        IBackupRunEventSink& events,
        IBackupRunCheckpointStore& checkpoints,
        CancellationToken& cancellation
    ) override;

  private:
    IBackupRunActionHandler& action_handler_;
    ITransferPipeline& transfers_;
    bool pin_transfer_paths_;
};

class FileBackupRunLeaseProvider final : public IBackupRunLeaseProvider {
  public:
    explicit FileBackupRunLeaseProvider(std::filesystem::path lock_root);
    [[nodiscard]] BackupRunLeaseResult try_acquire(const Profile& profile) override;

  private:
    std::filesystem::path lock_root_;
};

class FileRunStateRepository final : public IRunStateRepository {
  public:
    explicit FileRunStateRepository(ApplicationPaths paths);

    [[nodiscard]] bool last_success_matches(
        const Profile& profile,
        const std::string& date,
        const std::string& fingerprint
    ) const override;
    void write_skipped(
        const Profile& profile,
        const RunId& run_id,
        const std::string& started_at,
        const std::string& finished_at,
        std::size_t source_count
    ) override;
    void write_success(
        const Profile& profile,
        const RunId& run_id,
        const std::string& date,
        const std::string& timestamp,
        const std::string& fingerprint,
        std::size_t source_count
    ) override;
    [[nodiscard]] std::unique_ptr<IBackupRunCheckpointStore> checkpoints(const ProfileId& profile_id) override;
    [[nodiscard]] std::unique_ptr<IBackupRunEventSink> events(BackupRunStatusDescription description) override;
    void request_cancel(const ProfileId& profile_id) override;
    [[nodiscard]] bool cancel_requested(const ProfileId& profile_id) const override;
    void clear_cancel_request(const ProfileId& profile_id) override;

  private:
    [[nodiscard]] std::filesystem::path state_dir(const ProfileId& profile_id) const;
    ApplicationPaths paths_;
};

class FileCancellationMonitor final : public ICancellationMonitor {
  public:
    explicit FileCancellationMonitor(IRunStateRepository& state);
    [[nodiscard]] std::unique_ptr<ICancellationWatch> watch(
        const ProfileId& profile_id,
        CancellationToken& cancellation
    ) override;

  private:
    IRunStateRepository& state_;
};

class SystemClock final : public IClock {
  public:
    [[nodiscard]] std::string snapshot_timestamp() const override;
    [[nodiscard]] std::string local_date() const override;
    [[nodiscard]] std::string local_timestamp() const override;
};

class TimestampRunIdGenerator final : public IRunIdGenerator {
  public:
    [[nodiscard]] RunId generate(const std::string& snapshot_timestamp) override;
};

} // namespace btrfsbackup
