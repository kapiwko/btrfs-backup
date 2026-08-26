// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <backup/backup_run_executor.hpp>
#include <backup/ports/btrfs_operations.hpp>
#include <backup/ports/command_runner.hpp>
#include <backup/ports/filesystem.hpp>
#include <backup/ports/trusted_executable.hpp>

namespace btrfsbackup {

class SafeDirectoryRoot;

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
    ~BackupRunActionEffects() override;

    void execute_action(
        const BackupRunAction& action,
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
