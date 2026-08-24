#pragma once

#include <filesystem>
#include <memory>

#include <backup/backup_run_executor.hpp>
#include <platform/linux/btrfs_operations.hpp>
#include <platform/linux/command_runner.hpp>
#include <platform/linux/filesystem.hpp>
#include <platform/linux/trusted_executable.hpp>

namespace btrfsbackup {

class BackupRunActionEffects final : public IBackupRunActionEffects {
public:
    BackupRunActionEffects(IBtrfsOperations& btrfs, IFileSystem& fs_effects);
    BackupRunActionEffects(IBtrfsOperations& btrfs, IFileSystem& fs_effects, ICommandRunner& hooks);
    BackupRunActionEffects(
        IBtrfsOperations& btrfs,
        IFileSystem& fs_effects,
        ICommandRunner& hooks,
        const std::filesystem::path& target_mount_point
    );
    BackupRunActionEffects(
        IBtrfsOperations& btrfs,
        IFileSystem& fs_effects,
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
    IFileSystem& fs_effects_;
    ICommandRunner* hooks_ = nullptr;
    std::unique_ptr<SafeDirectoryRoot> local_root_;
    std::unique_ptr<SafeDirectoryRoot> target_root_;
    std::filesystem::path hook_root_path_;
    TrustedExecutablePolicy hook_policy_;
};

} // namespace btrfsbackup
