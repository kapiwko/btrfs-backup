// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string_view>

namespace btrfsbackup {

class IDurableFileOperations {
  public:
    virtual ~IDurableFileOperations() = default;

    virtual void ensure_directory(
        const std::filesystem::path& path,
        std::filesystem::perms permissions
    ) = 0;
    virtual void write_atomically(
        const std::filesystem::path& path,
        std::string_view data,
        std::filesystem::perms permissions
    ) = 0;
    virtual void remove_durably(const std::filesystem::path& path) = 0;
};

inline constexpr std::filesystem::perms private_file_permissions =
    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
inline constexpr std::filesystem::perms private_directory_permissions =
    private_file_permissions | std::filesystem::perms::owner_exec;
inline constexpr std::filesystem::perms public_read_file_permissions =
    private_file_permissions | std::filesystem::perms::group_read | std::filesystem::perms::others_read;
inline constexpr std::filesystem::perms public_directory_permissions =
    private_directory_permissions | std::filesystem::perms::group_read | std::filesystem::perms::group_exec | std::filesystem::perms::others_read | std::filesystem::perms::others_exec;

} // namespace btrfsbackup
