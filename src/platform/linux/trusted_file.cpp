// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/trusted_file.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <core/errors.hpp>

namespace btrfsbackup {

namespace {

class UniqueFd {
public:
    explicit UniqueFd(int fd) : fd_(fd) {}
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    ~UniqueFd() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    int get() const {
        return fd_;
    }

private:
    int fd_;
};

int open_trusted_config_file(const std::filesystem::path& path) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        if (errno == EACCES) {
            throw ValidationError("Configuration file is not readable: " + path.string());
        }
        throw ValidationError("Configuration file does not exist or is not a regular file: " + path.string());
    }
    return fd;
}

void assert_trusted_config_fd(int fd, const std::filesystem::path& path, const TrustedFilePolicy& policy) {
    struct stat info {};
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        throw ValidationError("Configuration file does not exist or is not a regular file: " + path.string());
    }

    uid_t current_uid = geteuid();
    if (info.st_uid != 0 && !(policy.allow_current_user_owner && info.st_uid == current_uid)) {
        throw ValidationError("Trusted profile JSON must be owned by root: " + path.string());
    }

    const mode_t forbidden_mode = policy.allow_group_other_read ? 0022 : 0077;
    if ((info.st_mode & forbidden_mode) != 0) {
        throw ValidationError(
            policy.allow_group_other_read
                ? "Trusted configuration file must not be writable by group or others: " + path.string()
                : "Trusted profile JSON must not be accessible by group or others: " + path.string()
        );
    }
}

std::string read_all(int fd, const std::filesystem::path& path) {
    std::string content;
    char buffer[8192];
    while (true) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            content.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) {
            return content;
        }
        if (errno == EINTR) {
            continue;
        }
        throw ValidationError("Configuration file is not readable: " + path.string() + ": " + std::strerror(errno));
    }
}

} // namespace

void assert_trusted_config_file(const std::filesystem::path& path, const TrustedFilePolicy& policy) {
    UniqueFd fd(open_trusted_config_file(path));
    assert_trusted_config_fd(fd.get(), path, policy);
}

std::string read_trusted_config_file(const std::filesystem::path& path, const TrustedFilePolicy& policy) {
    UniqueFd fd(open_trusted_config_file(path));
    assert_trusted_config_fd(fd.get(), path, policy);
    return read_all(fd.get(), path);
}

} // namespace btrfsbackup
