#pragma once

#include <filesystem>
#include <memory>

#include <btrfsbackup/engine/backup_run_executor.hpp>
#include <btrfsbackup/system/btrfs_operations.hpp>
#include <btrfsbackup/application/runtime_adapters.hpp>
#include <btrfsbackup/system/trusted_executable.hpp>

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
    BackupRunActionEffects(
        IBtrfsOperations& btrfs,
        IFileSystemEffects& fs_effects,
        ICommandRunner& hooks,
        const std::filesystem::path& target_mount_point,
        const std::filesystem::path& hook_root,
        const TrustedExecutablePolicy& hook_policy
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
    std::filesystem::path hook_root_path_;
    TrustedExecutablePolicy hook_policy_;
};

} // namespace btrfsbackup
