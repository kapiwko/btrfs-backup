// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include <daemon/control/BrowseSessionService.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>

namespace btrfsbackup::daemon::control {

class BrowseFilesystemAccess final {
  public:
    [[nodiscard]] static std::filesystem::path normalize_relative_path(
        const std::filesystem::path& relative_path
    );
    [[nodiscard]] std::vector<BrowseEntryInfo> list_directory(
        const std::filesystem::path& root,
        const std::filesystem::path& relative_path,
        std::size_t maximum_entries
    ) const;
    [[nodiscard]] BrowseDirectoryPage list_directory_page(
        const std::filesystem::path& root,
        const std::filesystem::path& relative_path,
        const std::string& after_name,
        std::size_t maximum_entries
    ) const;
    [[nodiscard]] BrowseEntryInfo inspect_entry(
        const std::filesystem::path& root,
        const std::filesystem::path& relative_path
    ) const;
    [[nodiscard]] btrfsbackup::platform::linux::OwnedFileDescriptor open_file(
        const std::filesystem::path& root,
        const std::filesystem::path& relative_path
    ) const;
    [[nodiscard]] btrfsbackup::platform::linux::OwnedFileDescriptor open_root(
        const std::filesystem::path& root
    ) const;
};

} // namespace btrfsbackup::daemon::control
