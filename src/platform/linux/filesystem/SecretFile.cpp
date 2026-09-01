// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/filesystem/SecretFile.hpp>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <vector>
#include <filesystem>

#include <core/Errors.hpp>

namespace btrfsbackup::platform::linux::filesystem {

namespace {

void erase(void* data, std::size_t size) noexcept {
    explicit_bzero(data, size);
}

void write_all(int fd, std::span<const std::byte> bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written = write(fd, bytes.data() + offset, bytes.size() - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        throw ValidationError(std::string("cannot create protected secret: ") + std::strerror(errno));
    }
}

OwnedFileDescriptor create_secret_file() {
    const int fd = memfd_create("btrfs-backup-secret", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0)
        throw ValidationError(std::string("cannot allocate protected secret: ") + std::strerror(errno));
    return OwnedFileDescriptor(fd);
}

void seal_and_rewind(int fd) {
    if (lseek(fd, 0, SEEK_SET) < 0)
        throw ValidationError(std::string("cannot rewind protected secret: ") + std::strerror(errno));
    if (fcntl(fd, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) < 0)
        throw ValidationError(std::string("cannot seal protected secret: ") + std::strerror(errno));
}

void sync_fd(int fd, const std::string& description) {
    int result;
    do {
        result = fsync(fd);
    } while (result < 0 && errno == EINTR);
    if (result < 0)
        throw ValidationError("cannot sync " + description + ": " + std::strerror(errno));
}

} // namespace

OwnedFileDescriptor create_sealed_secret_file(std::span<const std::byte> secret) {
    if (secret.empty())
        throw ValidationError("secret must not be empty");
    if (secret.size() > maximum_secret_bytes)
        throw ValidationError("secret exceeds the accepted size");
    OwnedFileDescriptor result = create_secret_file();
    write_all(result.get(), secret);
    seal_and_rewind(result.get());
    return result;
}

OwnedFileDescriptor copy_secret_to_sealed_file(int source_fd, std::size_t maximum_bytes) {
    if (source_fd < 0)
        throw ValidationError("secret descriptor is invalid");
    if (maximum_bytes == 0 || maximum_bytes > maximum_secret_bytes)
        throw ValidationError("secret size limit is invalid");

    std::vector<std::byte> secret;
    secret.reserve(maximum_bytes);
    std::array<std::byte, 512> buffer{};
    try {
        while (true) {
            const ssize_t count = read(source_fd, buffer.data(), buffer.size());
            if (count == 0)
                break;
            if (count < 0) {
                if (errno == EINTR)
                    continue;
                throw ValidationError(std::string("cannot read secret descriptor: ") + std::strerror(errno));
            }
            const std::size_t size = static_cast<std::size_t>(count);
            if (secret.size() + size > maximum_bytes)
                throw ValidationError("secret exceeds the accepted size");
            secret.insert(secret.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(size));
        }
        OwnedFileDescriptor result = create_sealed_secret_file(secret);
        erase(secret.data(), secret.size());
        erase(buffer.data(), buffer.size());
        return result;
    } catch (...) {
        erase(secret.data(), secret.size());
        erase(buffer.data(), buffer.size());
        throw;
    }
}

OwnedFileDescriptor generate_random_secret_file(std::size_t size) {
    if (size == 0 || size > maximum_secret_bytes)
        throw ValidationError("generated secret size is invalid");
    std::vector<std::byte> secret(size);
    std::size_t offset = 0;
    try {
        while (offset < size) {
            const ssize_t count = getrandom(secret.data() + offset, size - offset, 0);
            if (count > 0) {
                offset += static_cast<std::size_t>(count);
                continue;
            }
            if (count < 0 && errno == EINTR)
                continue;
            throw ValidationError(std::string("cannot generate secret: ") + std::strerror(errno));
        }
        OwnedFileDescriptor result = create_sealed_secret_file(secret);
        erase(secret.data(), secret.size());
        return result;
    } catch (...) {
        erase(secret.data(), secret.size());
        throw;
    }
}

void install_secret_file(int source_fd, const std::filesystem::path& destination, std::size_t maximum_bytes) {
    if (source_fd < 0)
        throw ValidationError("secret descriptor is invalid");
    if (!destination.is_absolute() || destination.lexically_normal() != destination)
        throw ValidationError("secret destination must be an absolute normalized path");
    if (lseek(source_fd, 0, SEEK_SET) < 0)
        throw ValidationError(std::string("cannot rewind secret: ") + std::strerror(errno));

    const std::filesystem::path parent = destination.parent_path();
    std::filesystem::create_directories(parent);
    std::string pattern = (parent / ("." + destination.filename().string() + ".XXXXXX")).string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    OwnedFileDescriptor output(mkstemp(writable.data()));
    if (!output.valid())
        throw ValidationError(std::string("cannot create secret file: ") + std::strerror(errno));
    const std::filesystem::path temporary(writable.data());
    std::array<std::byte, 512> buffer{};
    std::size_t total = 0;
    try {
        if (fchmod(output.get(), 0600) < 0)
            throw ValidationError(std::string("cannot protect secret file: ") + std::strerror(errno));
        while (true) {
            const ssize_t count = read(source_fd, buffer.data(), buffer.size());
            if (count == 0)
                break;
            if (count < 0) {
                if (errno == EINTR)
                    continue;
                throw ValidationError(std::string("cannot read secret: ") + std::strerror(errno));
            }
            const std::size_t size = static_cast<std::size_t>(count);
            total += size;
            if (total > maximum_bytes)
                throw ValidationError("secret exceeds the accepted size");
            write_all(output.get(), std::span(buffer.data(), size));
            erase(buffer.data(), size);
        }
        if (total == 0)
            throw ValidationError("secret must not be empty");
        sync_fd(output.get(), "secret file");
        output.reset();
        if (rename(temporary.c_str(), destination.c_str()) < 0)
            throw ValidationError(std::string("cannot publish secret file: ") + std::strerror(errno));
        OwnedFileDescriptor directory(open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        if (!directory.valid())
            throw ValidationError(std::string("cannot open secret directory: ") + std::strerror(errno));
        sync_fd(directory.get(), "secret directory");
        erase(buffer.data(), buffer.size());
    } catch (...) {
        erase(buffer.data(), buffer.size());
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
}

} // namespace btrfsbackup::platform::linux::filesystem
