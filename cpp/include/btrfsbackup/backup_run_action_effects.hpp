#pragma once

#include <btrfsbackup/backup_run_executor.hpp>
#include <btrfsbackup/btrfs_operations.hpp>
#include <btrfsbackup/runtime_adapters.hpp>

namespace btrfsbackup {

class BackupRunActionEffects final : public IBackupRunActionEffects {
public:
    BackupRunActionEffects(IBtrfsOperations& btrfs, IFileSystemEffects& fs_effects);

    void execute_action(
        const BackupRunAction& action,
        const BackupSourceRunPlan& source_plan,
        const BackupRunPlan& run_plan
    ) override;

private:
    IBtrfsOperations& btrfs_;
    IFileSystemEffects& fs_effects_;
};

} // namespace btrfsbackup
