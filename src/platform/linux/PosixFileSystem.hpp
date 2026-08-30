// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/IFileSystem.hpp>

namespace btrfsbackup::platform::linux {

class PosixFileSystem final : public btrfsbackup::backup::IFileSystem {
  public:
    bool exists(const std::filesystem::path& path) override;
    bool is_directory(const std::filesystem::path& path) override;
    void create_directories(const std::filesystem::path& path) override;
    void remove_file(const std::filesystem::path& path) override;
    void remove_directory(const std::filesystem::path& path) override;
    void remove_tree(const std::filesystem::path& path) override;
    void rename_path(const std::filesystem::path& source, const std::filesystem::path& target) override;
    std::vector<std::filesystem::path> list_directory(const std::filesystem::path& path) override;
};

} // namespace btrfsbackup::platform::linux
