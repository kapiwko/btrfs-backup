// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <sys/types.h>

#include <filesystem>
#include <string>
#include <system_error>

#include <platform/linux/filesystem/SafeDirectoryRoot.hpp>

namespace btrfsbackup::platform::linux::filesystem {

class SafeFilename {
  public:
    explicit SafeFilename(std::string value);

    [[nodiscard]] const std::string& value() const noexcept;

  private:
    std::string value_;
};

class TrustedDirectory {
  public:
    TrustedDirectory(
        const std::filesystem::path& path,
        const std::filesystem::path& trusted_root = "/",
        uid_t trusted_owner = 0
    );
    TrustedDirectory(const TrustedDirectory&) = delete;
    TrustedDirectory& operator=(const TrustedDirectory&) = delete;
    TrustedDirectory(TrustedDirectory&&) noexcept = default;
    TrustedDirectory& operator=(TrustedDirectory&&) noexcept = default;

    [[nodiscard]] int fd() const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] bool exists(const SafeFilename& filename) const;
    void rename_noreplace(
        const SafeFilename& source,
        const TrustedDirectory& destination,
        const SafeFilename& destination_filename
    ) const;
    [[nodiscard]] std::error_code remove(const SafeFilename& filename) const noexcept;
    void sync() const;

  private:
    std::filesystem::path path_;
    SafeDirectoryHandle directory_;
};

void validate_trusted_directory(
    const std::filesystem::path& path,
    const std::filesystem::path& trusted_root = "/",
    uid_t trusted_owner = 0
);

void ensure_trusted_directory(
    const std::filesystem::path& path,
    unsigned int mode,
    const std::filesystem::path& trusted_root = "/",
    uid_t trusted_owner = 0
);

} // namespace btrfsbackup::platform::linux::filesystem
