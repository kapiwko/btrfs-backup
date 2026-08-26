// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/action_handlers/backup_run_action_handler.hpp>
#include <backup/backup_service.hpp>
#include <backup/ports/command_runner.hpp>
#include <backup/ports/safe_directory.hpp>
#include <backup/model/snapshot_inventory.hpp>
#include <backup/transfer/transfer_pipeline.hpp>
#include <config/application_config.hpp>

namespace btrfsbackup {

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
    explicit DefaultBackupPlanner(
        SnapshotMetadataReader metadata_reader,
        const ISafeDirectoryRootFactory& safe_directories
    );

    [[nodiscard]] BackupRunPlan build(
        const Profile& profile,
        const std::vector<MountEntry>& mounts,
        const ApplicationPaths& paths,
        const RunId& run_id,
        const std::string& snapshot_timestamp
    ) const override;

  private:
    SnapshotMetadataReader metadata_reader_;
    const ISafeDirectoryRootFactory& safe_directories_;
};

class DefaultBackupRunFactory final : public IBackupRunFactory {
  public:
    DefaultBackupRunFactory(
        IBackupRunActionHandler& action_handler,
        ITransferPipeline& transfers,
        const ISafeDirectoryRootFactory& safe_directories
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
    const ISafeDirectoryRootFactory& safe_directories_;
};

} // namespace btrfsbackup
