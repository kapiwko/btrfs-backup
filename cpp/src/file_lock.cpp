#include <btrfsbackup/file_lock.hpp>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>
#include <utility>

#include <btrfsbackup/errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

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
    fd_ = open(path_.c_str(), O_CREAT | O_WRONLY | O_CLOEXEC, 0640);
    if (fd_ < 0) {
        throw ValidationError("cannot open lock file: " + path_.string());
    }
    if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        close(fd_);
        fd_ = -1;
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
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
