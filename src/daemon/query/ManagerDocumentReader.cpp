// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/query/ManagerDocumentReader.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <system_error>

#include <core/Errors.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>

namespace fs = std::filesystem;

namespace {

constexpr std::size_t max_document_bytes = 1024 * 1024;

} // namespace

namespace btrfsbackup::daemon::query {

btrfsbackup::config::Json read_manager_json_document(const fs::path& path) {
    btrfsbackup::platform::linux::OwnedFileDescriptor descriptor(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (descriptor.get() < 0) {
        throw ValidationError("cannot read manager data file " + path.string());
    }
    struct stat info{};
    if (fstat(descriptor.get(), &info) != 0 || !S_ISREG(info.st_mode)) {
        throw ValidationError("manager data path is not a regular file: " + path.string());
    }
    if (info.st_size < 0 || static_cast<std::uintmax_t>(info.st_size) > max_document_bytes) {
        throw ValidationError("manager data file exceeds the size limit: " + path.string());
    }
    if ((info.st_mode & 0022) != 0) {
        throw ValidationError("manager data file is writable by group or others: " + path.string());
    }

    std::string content;
    content.reserve(static_cast<std::size_t>(info.st_size));
    char buffer[8192];
    while (true) {
        const ssize_t count = read(descriptor.get(), buffer, sizeof(buffer));
        if (count > 0) {
            if (content.size() + static_cast<std::size_t>(count) > max_document_bytes) {
                throw ValidationError("manager data file exceeds the size limit: " + path.string());
            }
            content.append(buffer, static_cast<std::size_t>(count));
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            throw ValidationError(
                "cannot read manager data file " + path.string() + ": " + std::strerror(errno)
            );
        }
    }
    try {
        return btrfsbackup::config::Json::parse(content);
    } catch (const std::exception& error) {
        throw ValidationError("invalid manager JSON " + path.string() + ": " + error.what());
    }
}

bool manager_regular_file_without_symlink(const fs::directory_entry& entry) {
    std::error_code error;
    return entry.symlink_status(error).type() == fs::file_type::regular && !error;
}

bool manager_regular_file_if_present(const fs::path& path) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) {
        return false;
    }
    if (error) {
        throw ValidationError("cannot inspect manager data file " + path.string());
    }
    return status.type() == fs::file_type::regular;
}

} // namespace btrfsbackup::daemon::query
