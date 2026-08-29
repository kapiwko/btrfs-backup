// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/manager_audit_log.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <config/model/json.hpp>
#include <core/runtime_time.hpp>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void throw_audit_error(const fs::path& path, const std::string& operation, int error) {
    throw std::runtime_error(operation + " " + path.string() + ": " + std::strerror(error));
}

void require_secure_audit_file(int descriptor, const fs::path& path) {
    struct stat status{};
    if (fstat(descriptor, &status) < 0)
        throw_audit_error(path, "cannot inspect manager audit log", errno);
    if (!S_ISREG(status.st_mode))
        throw std::runtime_error("manager audit log is not a regular file: " + path.string());
    if (status.st_uid != geteuid())
        throw std::runtime_error("manager audit log has an unexpected owner: " + path.string());
    if (fchmod(descriptor, S_IRUSR | S_IWUSR) < 0)
        throw_audit_error(path, "cannot secure manager audit log", errno);
}

void write_all(int descriptor, const std::string& data, const fs::path& path) {
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t result = ::write(descriptor, data.data() + written, data.size() - written);
        if (result < 0) {
            if (errno == EINTR)
                continue;
            throw_audit_error(path, "cannot append manager audit log", errno);
        }
        if (result == 0)
            throw std::runtime_error("cannot append manager audit log " + path.string() + ": write returned zero bytes");
        written += static_cast<std::size_t>(result);
    }
}

} // namespace

namespace btrfsbackup::daemon {

FileManagerAuditLog::FileManagerAuditLog(const fs::path& path) : path_(path) {
    if (!path_.is_absolute())
        throw std::invalid_argument("manager audit log path must be absolute");
    const fs::path parent = path_.parent_path();
    const fs::file_status parent_status = fs::symlink_status(parent);
    if (fs::exists(parent_status)) {
        if (fs::is_symlink(parent_status) || !fs::is_directory(parent_status))
            throw std::runtime_error("manager audit log parent is not a trusted directory: " + parent.string());
    } else {
        fs::create_directories(parent);
        fs::permissions(parent, fs::perms::owner_all, fs::perm_options::replace);
    }
    do {
        descriptor_ = open(
            path_.c_str(),
            O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
            S_IRUSR | S_IWUSR
        );
    } while (descriptor_ < 0 && errno == EINTR);
    if (descriptor_ < 0)
        throw_audit_error(path_, "cannot open manager audit log", errno);
    try {
        require_secure_audit_file(descriptor_, path_);
    } catch (...) {
        close(descriptor_);
        descriptor_ = -1;
        throw;
    }
}

FileManagerAuditLog::~FileManagerAuditLog() {
    if (descriptor_ >= 0)
        close(descriptor_);
}

std::optional<std::string> FileManagerAuditLog::write(const ManagerAuditRecord& record) noexcept {
    try {
        std::string data = btrfsbackup::config::Json{
            {"schemaVersion", 1},
            {"timestamp", format_utc_iso_timestamp(std::chrono::system_clock::now())},
            {"callerUid", record.caller_uid},
            {"action", record.action},
            {"profileId", record.profile_id},
            {"result", record.result},
            {"errorCode", record.error_code},
        }
                               .dump();
        data.push_back('\n');

        if (flock(descriptor_, LOCK_EX) < 0)
            throw_audit_error(path_, "cannot lock manager audit log", errno);
        try {
            write_all(descriptor_, data, path_);
            if (fdatasync(descriptor_) < 0)
                throw_audit_error(path_, "cannot sync manager audit log", errno);
        } catch (...) {
            (void)flock(descriptor_, LOCK_UN);
            throw;
        }
        if (flock(descriptor_, LOCK_UN) < 0)
            throw_audit_error(path_, "cannot unlock manager audit log", errno);
        return std::nullopt;
    } catch (const std::exception& error) {
        return error.what();
    } catch (...) {
        return "unknown manager audit log failure";
    }
}

} // namespace btrfsbackup::daemon
