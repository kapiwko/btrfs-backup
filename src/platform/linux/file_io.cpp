// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/file_io.hpp>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vector>

#include <core/errors.hpp>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void throw_file_error(const std::string& operation, const fs::path& path, int error) {
    throw btrfsbackup::ValidationError(operation + " " + path.string() + ": " + std::strerror(error));
}

void sync_fd(int fd, const fs::path& path, const std::string& operation) {
    int result;
    do {
        result = fsync(fd);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        throw_file_error(operation, path, errno);
    }
}

void close_checked(int& fd, const fs::path& path, const std::string& operation) {
    const int descriptor = fd;
    fd = -1;
    if (close(descriptor) < 0) {
        throw_file_error(operation, path, errno);
    }
}

} // namespace

namespace btrfsbackup {

void atomic_write(const fs::path& path, const std::string& data, mode_t mode) {
    const fs::path parent = path.has_parent_path() ? path.parent_path() : fs::path(".");
    fs::create_directories(parent);
    std::string pattern = (parent / ("." + path.filename().string() + ".XXXXXX")).string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    int fd = mkstemp(writable.data());
    if (fd < 0) {
        throw_file_error("cannot create temporary file for", path, errno);
    }
    fs::path temporary(writable.data());
    try {
        int chmod_result;
        do {
            chmod_result = fchmod(fd, mode);
        } while (chmod_result < 0 && errno == EINTR);
        if (chmod_result < 0) {
            throw_file_error("cannot set permissions on", temporary, errno);
        }

        const char* ptr = data.data();
        std::size_t remaining = data.size();
        while (remaining > 0) {
            const std::size_t chunk_size = std::min(remaining, static_cast<std::size_t>(SSIZE_MAX));
            ssize_t written = write(fd, ptr, chunk_size);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw_file_error("cannot write", temporary, errno);
            }
            if (written == 0) {
                throw ValidationError("cannot write " + temporary.string() + ": write returned zero bytes");
            }
            ptr += written;
            remaining -= static_cast<std::size_t>(written);
        }

        sync_fd(fd, temporary, "cannot sync");
        close_checked(fd, temporary, "cannot close");

        int rename_result;
        do {
            rename_result = rename(temporary.c_str(), path.c_str());
        } while (rename_result < 0 && errno == EINTR);
        if (rename_result < 0) {
            throw_file_error("cannot rename " + temporary.string() + " to", path, errno);
        }

        fsync_dir(parent);
    } catch (...) {
        if (fd >= 0) {
            close(fd);
        }
        std::error_code ec;
        fs::remove(temporary, ec);
        throw;
    }
}

void fsync_dir(const fs::path& path) {
    int fd;
    do {
        fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        throw_file_error("cannot open directory", path, errno);
    }

    try {
        sync_fd(fd, path, "cannot sync directory");
        close_checked(fd, path, "cannot close directory");
    } catch (...) {
        if (fd >= 0) {
            close(fd);
        }
        throw;
    }
}

void PosixDurableFileOperations::ensure_directory(const fs::path& path, fs::perms permissions) {
    fs::create_directories(path);
    std::error_code error;
    fs::permissions(path, permissions, fs::perm_options::replace, error);
    if (error) {
        throw ValidationError("cannot set permissions on " + path.string() + ": " + error.message());
    }
}

void PosixDurableFileOperations::write_atomically(
    const fs::path& path,
    std::string_view data,
    fs::perms permissions
) {
    atomic_write(path, std::string(data), static_cast<mode_t>(permissions));
}

void PosixDurableFileOperations::remove_durably(const fs::path& path) {
    std::error_code error;
    const bool removed = fs::remove(path, error);
    if (error) {
        throw ValidationError("cannot remove " + path.string() + ": " + error.message());
    }
    if (removed) {
        fsync_dir(path.parent_path());
    }
}

} // namespace btrfsbackup
