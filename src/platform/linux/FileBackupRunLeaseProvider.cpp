// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/FileBackupRunLeaseProvider.hpp>

#include <memory>
#include <string>
#include <utility>

#include <core/ErrorCode.hpp>
#include <platform/linux/FileLock.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux {

namespace {

class FileBackupRunLease final : public btrfsbackup::backup::IBackupRunLease {
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

btrfsbackup::backup::BackupRunLeaseResult FileBackupRunLeaseProvider::try_acquire(const btrfsbackup::config::Profile& profile) {
    FileLock profile_lock(profile_lock_path(lock_root_, std::string(profile.id.value())));
    if (!profile_lock.try_acquire()) {
        return btrfsbackup::backup::BackupRunLeaseBusy{
            .error_code = ErrorCode::RunnerProfileBusy,
            .error_message = "Another runner is already active for profile " + std::string(profile.id.value()) + ".",
        };
    }
    FileLock target_lock(target_lock_path(lock_root_, profile.target.luks_uuid.value()));
    if (!target_lock.try_acquire()) {
        return btrfsbackup::backup::BackupRunLeaseBusy{
            .error_code = ErrorCode::RunnerTargetBusy,
            .error_message = "Another operation is already active for target LUKS UUID " +
                profile.target.luks_uuid.value() + ".",
        };
    }
    return btrfsbackup::backup::BackupRunLeaseAcquired{
        .lease = std::make_unique<FileBackupRunLease>(std::move(profile_lock), std::move(target_lock)),
    };
}

} // namespace btrfsbackup::platform::linux
