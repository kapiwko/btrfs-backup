#include <btrfsbackup/file_lock.hpp>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <filesystem>
#include <string>
#include <utility>

#include <btrfsbackup/errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

fs::path default_lock_root() {
    return "/run/btrfs-backup/locks";
}

fs::path profile_lock_path(const fs::path& lock_root, const std::string& profile_id) {
    return lock_root / "profiles" / (profile_id + ".lock");
}

fs::path target_lock_path(const fs::path& lock_root, const std::string& luks_uuid) {
    std::string normalized_uuid = luks_uuid;
    std::transform(normalized_uuid.begin(), normalized_uuid.end(), normalized_uuid.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lock_root / "targets" / (normalized_uuid + ".lock");
}

FileLock::FileLock(fs::path path) : path_(std::move(path)) {}

FileLock::FileLock(FileLock&& other) noexcept
    : path_(std::move(other.path_)), fd_(other.fd_), acquired_(other.acquired_) {
    other.fd_ = -1;
    other.acquired_ = false;
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
        release();
        path_ = std::move(other.path_);
        fd_ = other.fd_;
        acquired_ = other.acquired_;
        other.fd_ = -1;
        other.acquired_ = false;
    }
    return *this;
}

FileLock::~FileLock() {
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
    return true;
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
}

bool FileLock::acquired() const {
    return acquired_;
}

} // namespace btrfsbackup
