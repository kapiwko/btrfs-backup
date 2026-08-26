// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/trusted_executable.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <core/errors.hpp>
#include <config/model/validation.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

class PosixTrustedExecutable final : public btrfsbackup::ITrustedExecutable {
  public:
    explicit PosixTrustedExecutable(btrfsbackup::SafeDirectoryHandle handle)
        : handle_(std::move(handle)) {
    }

    std::string execution_path() const override {
        return handle_.proc_path().string();
    }

    std::vector<int> inherited_fds() const override {
        return {handle_.fd()};
    }

  private:
    btrfsbackup::SafeDirectoryHandle handle_;
};

bool trusted_owner(uid_t owner, const TrustedExecutablePolicy& policy) {
    return owner == 0 || (policy.allow_current_user_owner && owner == geteuid());
}

struct stat descriptor_status(int fd, const fs::path& path) {
    struct stat status{};
    if (fstat(fd, &status) != 0) {
        throw ValidationError("cannot inspect trusted hook path: " + path.string());
    }
    return status;
}

void verify_directory(
    const SafeDirectoryHandle& handle,
    const fs::path& path,
    const TrustedExecutablePolicy& policy
) {
    struct stat status = descriptor_status(handle.fd(), path);
    if (!S_ISDIR(status.st_mode)) {
        throw ValidationError("trusted hook parent is not a directory: " + path.string());
    }
    if (!trusted_owner(status.st_uid, policy)) {
        throw ValidationError("trusted hook parent must be owned by root: " + path.string());
    }
    if ((status.st_mode & 0022) != 0) {
        throw ValidationError("trusted hook parent must not be writable by group or others: " + path.string());
    }
}

void verify_parent_directories(
    const SafeDirectoryRoot& trusted_root,
    const TrustedExecutablePolicy& policy
) {
    SafeDirectoryRoot filesystem_root("/");
    fs::path current = "/";
    verify_directory(filesystem_root.open_directory(current), current, policy);
    for (const fs::path& component : trusted_root.path().lexically_relative("/")) {
        if (component == ".") {
            continue;
        }
        current /= component;
        verify_directory(filesystem_root.open_directory(current), current, policy);
    }
}

} // namespace

SafeDirectoryHandle open_trusted_executable(
    const SafeDirectoryRoot& trusted_root,
    const fs::path& program,
    const TrustedExecutablePolicy& policy
) {
    fs::path normalized = normalized_path(program);
    if (!normalized.is_absolute() || normalized.parent_path() != trusted_root.path() || normalized.filename().empty()) {
        throw ValidationError("hook program must be a direct child of " + trusted_root.path().string());
    }
    if (policy.verify_parent_directories) {
        verify_parent_directories(trusted_root, policy);
    }

    SafeDirectoryHandle executable = trusted_root.open_path(normalized);
    struct stat status = descriptor_status(executable.fd(), normalized);
    if (!S_ISREG(status.st_mode)) {
        throw ValidationError("hook program is not a regular file: " + normalized.string());
    }
    if (!trusted_owner(status.st_uid, policy)) {
        throw ValidationError("hook program must be owned by root: " + normalized.string());
    }
    if ((status.st_mode & 0022) != 0) {
        throw ValidationError("hook program must not be writable by group or others: " + normalized.string());
    }
    if ((status.st_mode & 0111) == 0) {
        throw ValidationError("hook program is not executable: " + normalized.string());
    }
    return executable;
}

PosixTrustedExecutableResolver::PosixTrustedExecutableResolver(
    fs::path trusted_root,
    TrustedExecutablePolicy policy
)
    : trusted_root_(std::move(trusted_root)), policy_(policy) {
}

std::unique_ptr<ITrustedExecutable> PosixTrustedExecutableResolver::resolve(const fs::path& program) const {
    SafeDirectoryRoot trusted_root(trusted_root_);
    return std::make_unique<PosixTrustedExecutable>(
        open_trusted_executable(trusted_root, program, policy_)
    );
}

} // namespace btrfsbackup
