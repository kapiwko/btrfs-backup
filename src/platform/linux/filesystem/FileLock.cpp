// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/filesystem/FileLock.hpp>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>
#include <string>
#include <utility>

#include <core/Errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux::filesystem {

fs::path default_lock_root() {
    return "/run/btrfs-backup/locks";
}

fs::path profile_lock_path(const fs::path& lock_root, const ProfileId& profile_id) {
    return lock_root / "profiles" / (std::string(profile_id.value()) + ".lock");
}

fs::path target_lock_path(const fs::path& lock_root, const btrfsbackup::config::LuksUuid& luks_uuid) {
    return lock_root / "targets" / (luks_uuid.value() + ".lock");
}

FileLock::FileLock(fs::path path) : path_(std::move(path)) {
}

FileLock::FileLock(FileLock&& other) noexcept
    : path_(std::move(other.path_)), fd_(other.fd_), acquired_(other.acquired_), shared_(other.shared_) {
    other.fd_ = -1;
    other.acquired_ = false;
    other.shared_ = false;
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
        release();
        path_ = std::move(other.path_);
        fd_ = other.fd_;
        acquired_ = other.acquired_;
        shared_ = other.shared_;
        other.fd_ = -1;
        other.acquired_ = false;
        other.shared_ = false;
    }
    return *this;
}

FileLock::~FileLock() noexcept {
    release();
}

bool FileLock::try_acquire() {
    if (acquired_) {
        return true;
    }
    fs::create_directories(path_.parent_path());
    fd_ = open(path_.c_str(), O_CREAT | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0640);
    if (fd_ < 0) {
        throw ValidationError("cannot open lock file: " + path_.string());
    }
    if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        const int lock_error = errno;
        close(fd_);
        fd_ = -1;
        if (lock_error == EWOULDBLOCK || lock_error == EAGAIN) {
            return false;
        }
        throw ValidationError("cannot acquire lock file: " + path_.string());
    }
    acquired_ = true;
    shared_ = false;
    return true;
}

bool FileLock::try_acquire_shared() {
    if (acquired_)
        return true;
    fs::create_directories(path_.parent_path());
    fd_ = open(path_.c_str(), O_CREAT | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0640);
    if (fd_ < 0)
        throw ValidationError("cannot open lock file: " + path_.string());
    if (flock(fd_, LOCK_SH | LOCK_NB) != 0) {
        const int lock_error = errno;
        close(fd_);
        fd_ = -1;
        if (lock_error == EWOULDBLOCK || lock_error == EAGAIN)
            return false;
        throw ValidationError("cannot acquire shared lock file: " + path_.string());
    }
    acquired_ = true;
    shared_ = true;
    return true;
}

bool FileLock::try_upgrade_to_exclusive() {
    if (!acquired_)
        return try_acquire();
    if (!shared_)
        return true;
    if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return false;
        throw ValidationError("cannot upgrade lock file: " + path_.string());
    }
    shared_ = false;
    return true;
}

void FileLock::downgrade_to_shared() {
    if (!acquired_ || shared_)
        return;
    if (flock(fd_, LOCK_SH) != 0)
        throw ValidationError("cannot downgrade lock file: " + path_.string());
    shared_ = true;
}

void FileLock::release() {
    if (fd_ < 0) {
        acquired_ = false;
        return;
    }
    if (acquired_) {
        flock(fd_, LOCK_UN);
    }
    close(fd_);
    fd_ = -1;
    acquired_ = false;
    shared_ = false;
}

bool FileLock::acquired() const noexcept {
    return acquired_;
}

} // namespace btrfsbackup::platform::linux::filesystem
