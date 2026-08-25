// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <vector>

namespace btrfsbackup {

class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    virtual bool exists(const std::filesystem::path& path) = 0;
    virtual bool is_directory(const std::filesystem::path& path) = 0;
    virtual void create_directories(const std::filesystem::path& path) = 0;
    virtual void remove_file(const std::filesystem::path& path) = 0;
    virtual void remove_directory(const std::filesystem::path& path) = 0;
    virtual void remove_tree(const std::filesystem::path& path) = 0;
    virtual void rename_path(const std::filesystem::path& source, const std::filesystem::path& target) = 0;
    virtual std::vector<std::filesystem::path> list_directory(const std::filesystem::path& path) = 0;
};

class PosixFileSystem final : public IFileSystem {
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

} // namespace btrfsbackup
