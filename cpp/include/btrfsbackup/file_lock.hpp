#pragma once

#include <filesystem>

namespace btrfsbackup {

class FileLock {
public:
    explicit FileLock(std::filesystem::path path);
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;
    ~FileLock();

    bool try_acquire();
    void release();
    bool acquired() const;

private:
    std::filesystem::path path_;
    int fd_ = -1;
    bool acquired_ = false;
};

} // namespace btrfsbackup
