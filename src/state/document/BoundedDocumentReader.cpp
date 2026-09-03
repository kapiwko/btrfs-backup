// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/document/BoundedDocumentReader.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>

#include <core/Errors.hpp>

namespace fs = std::filesystem;

namespace {

class FileDescriptor {
  public:
    explicit FileDescriptor(int descriptor) noexcept
        : descriptor_(descriptor) {
    }

    ~FileDescriptor() noexcept {
        if (descriptor_ >= 0) {
            close(descriptor_);
        }
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&&) = delete;
    FileDescriptor& operator=(FileDescriptor&&) = delete;

    [[nodiscard]] int get() const noexcept {
        return descriptor_;
    }

  private:
    int descriptor_;
};

[[noreturn]] void throw_read_error(const fs::path& path, int error) {
    throw btrfsbackup::ValidationError(
        "cannot read document " + path.string() + ": " + std::strerror(error)
    );
}

} // namespace

namespace btrfsbackup::state {
namespace document {

std::string BoundedDocumentReader::read(
    const fs::path& path,
    std::size_t maximum_size,
    std::optional<std::uint32_t> expected_owner,
    std::optional<std::uint32_t> expected_permissions
) const {
    int raw_descriptor;
    do {
        raw_descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    } while (raw_descriptor < 0 && errno == EINTR);
    const FileDescriptor descriptor(raw_descriptor);
    if (descriptor.get() < 0) {
        throw_read_error(path, errno);
    }

    struct stat info{};
    int stat_result;
    do {
        stat_result = fstat(descriptor.get(), &info);
    } while (stat_result < 0 && errno == EINTR);
    if (stat_result < 0) {
        throw_read_error(path, errno);
    }
    if (!S_ISREG(info.st_mode)) {
        throw ValidationError("document path is not a regular file: " + path.string());
    }
    if (info.st_size < 0 || static_cast<std::uintmax_t>(info.st_size) > maximum_size) {
        throw ValidationError("document exceeds the size limit: " + path.string());
    }
    if ((info.st_mode & 0022) != 0) {
        throw ValidationError("document is writable by group or others: " + path.string());
    }
    if (expected_owner.has_value() && info.st_uid != *expected_owner) {
        throw ValidationError("document has an unexpected owner: " + path.string());
    }
    if (expected_permissions.has_value() && (info.st_mode & 0777) != *expected_permissions) {
        throw ValidationError("document has unexpected permissions: " + path.string());
    }

    std::string content;
    content.reserve(static_cast<std::size_t>(info.st_size));
    char buffer[8192];
    while (true) {
        const ssize_t count = ::read(descriptor.get(), buffer, sizeof(buffer));
        if (count > 0) {
            const std::size_t chunk_size = static_cast<std::size_t>(count);
            if (chunk_size > maximum_size - content.size()) {
                throw ValidationError("document exceeds the size limit: " + path.string());
            }
            content.append(buffer, chunk_size);
        } else if (count == 0) {
            return content;
        } else if (errno != EINTR) {
            throw_read_error(path, errno);
        }
    }
}

} // namespace document
} // namespace btrfsbackup::state
