// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <platform/linux/file_io.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

enum class FailurePoint {
    None,
    Fchmod,
    Write,
    FileFsync,
    FileClose,
    Rename,
    DirectoryFsync,
    DirectoryClose,
};

FailurePoint failure_point = FailurePoint::None;
int temporary_fd = -1;
bool interrupt_next_write = false;

std::string read_file(const fs::path& path) {
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool has_temporary_file(const fs::path& directory, const std::string& filename) {
    const std::string prefix = "." + filename + ".";
    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        if (entry.path().filename().string().starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

void reset_faults() {
    failure_point = FailurePoint::None;
    temporary_fd = -1;
    interrupt_next_write = false;
}

void expect_atomic_failure(const std::string& name, FailurePoint point, const std::string& expected_error) {
    const fs::path root = test_helpers::test_root("file-io", name);
    const fs::path path = root / "state";
    test_helpers::write_file(path, "old");

    failure_point = point;
    test_helpers::expect_validation_error(
        name,
        [&] { btrfsbackup::atomic_write(path, "new", 0600); },
        expected_error);
    reset_faults();

    if (point == FailurePoint::DirectoryFsync || point == FailurePoint::DirectoryClose) {
        test_helpers::expect_eq(name + " published content", read_file(path), "new");
    } else {
        test_helpers::expect_eq(name + " preserved content", read_file(path), "old");
    }
    test_helpers::expect_true(
        name + " temporary cleanup",
        !has_temporary_file(root, path.filename().string()),
        "temporary file should be removed");
}

void test_atomic_write_replaces_file_with_requested_mode() {
    const fs::path root = test_helpers::test_root("file-io", "success");
    const fs::path path = root / "state";
    test_helpers::write_file(path, "old");

    btrfsbackup::atomic_write(path, "new content", 0640);

    struct stat metadata{};
    test_helpers::expect_eq("atomic content", read_file(path), "new content");
    test_helpers::expect_true("atomic stat", stat(path.c_str(), &metadata) == 0, "stat should succeed");
    test_helpers::expect_eq("atomic mode", std::to_string(metadata.st_mode & 0777), std::to_string(0640));
    test_helpers::expect_true(
        "atomic no temporary",
        !has_temporary_file(root, path.filename().string()),
        "temporary file should not remain");
}

void test_write_retries_eintr() {
    const fs::path root = test_helpers::test_root("file-io", "eintr");
    const fs::path path = root / "state";
    interrupt_next_write = true;

    btrfsbackup::atomic_write(path, "written after interruption", 0600);
    reset_faults();

    test_helpers::expect_eq("write EINTR retry", read_file(path), "written after interruption");
}

void test_failures_are_reported() {
    expect_atomic_failure("fchmod failure", FailurePoint::Fchmod, "cannot set permissions");
    expect_atomic_failure("write failure", FailurePoint::Write, "cannot write");
    expect_atomic_failure("file fsync failure", FailurePoint::FileFsync, "cannot sync");
    expect_atomic_failure("file close failure", FailurePoint::FileClose, "cannot close");
    expect_atomic_failure("rename failure", FailurePoint::Rename, "cannot rename");
    expect_atomic_failure("directory fsync failure", FailurePoint::DirectoryFsync, "cannot sync directory");
    expect_atomic_failure("directory close failure", FailurePoint::DirectoryClose, "cannot close directory");
}

void test_fsync_dir_reports_open_failure() {
    const fs::path root = test_helpers::test_root("file-io", "directory-open");
    test_helpers::expect_validation_error(
        "directory open failure",
        [&] { btrfsbackup::fsync_dir(root / "missing"); },
        "cannot open directory");
}

} // namespace

extern "C" {

int __real_close(int fd);
int __real_fchmod(int fd, mode_t mode);
int __real_fsync(int fd);
int __real_mkstemp(char* pattern);
int __real_rename(const char* old_path, const char* new_path);
ssize_t __real_write(int fd, const void* buffer, size_t count);

int __wrap_mkstemp(char* pattern) {
    temporary_fd = __real_mkstemp(pattern);
    return temporary_fd;
}

int __wrap_fchmod(int fd, mode_t mode) {
    if (fd == temporary_fd && failure_point == FailurePoint::Fchmod) {
        errno = EPERM;
        return -1;
    }
    return __real_fchmod(fd, mode);
}

ssize_t __wrap_write(int fd, const void* buffer, size_t count) {
    if (fd == temporary_fd && interrupt_next_write) {
        interrupt_next_write = false;
        errno = EINTR;
        return -1;
    }
    if (fd == temporary_fd && failure_point == FailurePoint::Write) {
        errno = ENOSPC;
        return -1;
    }
    return __real_write(fd, buffer, count);
}

int __wrap_fsync(int fd) {
    const bool file_sync = fd == temporary_fd;
    if ((file_sync && failure_point == FailurePoint::FileFsync)
        || (!file_sync && failure_point == FailurePoint::DirectoryFsync)) {
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
}

int __wrap_close(int fd) {
    const bool temporary_close = fd == temporary_fd;
    const int result = __real_close(fd);
    if (temporary_close) {
        temporary_fd = -1;
    }
    if ((temporary_close && failure_point == FailurePoint::FileClose)
        || (!temporary_close && failure_point == FailurePoint::DirectoryClose)) {
        errno = EIO;
        return -1;
    }
    return result;
}

int __wrap_rename(const char* old_path, const char* new_path) {
    if (failure_point == FailurePoint::Rename) {
        errno = EIO;
        return -1;
    }
    return __real_rename(old_path, new_path);
}

} // extern "C"

int main() {
    test_atomic_write_replaces_file_with_requested_mode();
    test_write_retries_eintr();
    test_failures_are_reported();
    test_fsync_dir_reports_open_failure();
    return test_helpers::finish("file I/O tests");
}
