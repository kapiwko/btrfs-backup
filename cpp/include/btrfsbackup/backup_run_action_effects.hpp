#pragma once

#include <filesystem>
#include <memory>

#include <btrfsbackup/backup_run_executor.hpp>
#include <btrfsbackup/btrfs_operations.hpp>
#include <btrfsbackup/runtime_adapters.hpp>

namespace btrfsbackup {

class BackupRunActionEffects final : public IBackupRunActionEffects {
public:
    BackupRunActionEffects(IBtrfsOperations& btrfs, IFileSystemEffects& fs_effects);
    BackupRunActionEffects(IBtrfsOperations& btrfs, IFileSystemEffects& fs_effects, ICommandRunner& hooks);
    BackupRunActionEffects(
        IBtrfsOperations& btrfs,
        IFileSystemEffects& fs_effects,
        ICommandRunner& hooks,
        const std::filesystem::path& target_mount_point
    );

    void execute_action(
        const BackupRunAction& action,
        const BackupSourceRunPlan& source_plan,
        const BackupRunPlan& run_plan,
        CancellationToken& cancellation
    ) override;

private:
    IBtrfsOperations& btrfs_;
    IFileSystemEffects& fs_effects_;
    ICommandRunner* hooks_ = nullptr;
    std::unique_ptr<SafeDirectoryRoot> local_root_;
    std::unique_ptr<SafeDirectoryRoot> target_root_;
};

} // namespace btrfsbackup
