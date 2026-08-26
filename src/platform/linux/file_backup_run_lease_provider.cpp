// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/file_backup_run_lease_provider.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <core/error_code.hpp>
#include <platform/linux/file_lock.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

class FileBackupRunLease final : public IBackupRunLease {
  public:
    FileBackupRunLease(FileLock profile_lock, FileLock target_lock)
        : profile_lock_(std::move(profile_lock)), target_lock_(std::move(target_lock)) {
    }

  private:
    FileLock profile_lock_;
    FileLock target_lock_;
};

} // namespace

FileBackupRunLeaseProvider::FileBackupRunLeaseProvider(fs::path lock_root) : lock_root_(std::move(lock_root)) {
}

BackupRunLeaseResult FileBackupRunLeaseProvider::try_acquire(const Profile& profile) {
    FileLock profile_lock(profile_lock_path(lock_root_, std::string(profile.id.value())));
    if (!profile_lock.try_acquire()) {
        return {
            .lease = nullptr,
            .error_code = ErrorCode::RunnerProfileBusy,
            .error_message = "Another runner is already active for profile " + std::string(profile.id.value()) + ".",
        };
    }
    FileLock target_lock(target_lock_path(lock_root_, profile.target.luks_uuid));
    if (!target_lock.try_acquire()) {
        return {
            .lease = nullptr,
            .error_code = ErrorCode::RunnerTargetBusy,
            .error_message = "Another operation is already active for target LUKS UUID " + profile.target.luks_uuid + ".",
        };
    }
    return {
        .lease = std::make_unique<FileBackupRunLease>(std::move(profile_lock), std::move(target_lock)),
        .error_code = std::nullopt,
        .error_message = {},
    };
}

} // namespace btrfsbackup
