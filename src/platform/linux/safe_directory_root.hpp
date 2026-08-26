// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <backup/transfer/transfer_plan.hpp>

namespace btrfsbackup {

class SafeDirectoryHandle final : public ITransferResource {
public:
    SafeDirectoryHandle() = default;
    explicit SafeDirectoryHandle(int fd);
    SafeDirectoryHandle(const SafeDirectoryHandle&) = delete;
    SafeDirectoryHandle& operator=(const SafeDirectoryHandle&) = delete;
    SafeDirectoryHandle(SafeDirectoryHandle&& other) noexcept;
    SafeDirectoryHandle& operator=(SafeDirectoryHandle&& other) noexcept;
    ~SafeDirectoryHandle() override;

    int fd() const noexcept;
    std::filesystem::path proc_path() const;

private:
    int fd_ = -1;
};

class SafeDirectoryRoot {
public:
    explicit SafeDirectoryRoot(const std::filesystem::path& root);
    SafeDirectoryRoot(const SafeDirectoryRoot&) = delete;
    SafeDirectoryRoot& operator=(const SafeDirectoryRoot&) = delete;
    SafeDirectoryRoot(SafeDirectoryRoot&&) noexcept = default;
    SafeDirectoryRoot& operator=(SafeDirectoryRoot&&) noexcept = default;

    const std::filesystem::path& path() const noexcept;
    SafeDirectoryHandle open_directory(const std::filesystem::path& path) const;
    SafeDirectoryHandle open_path(const std::filesystem::path& path) const;
    void ensure_directory(const std::filesystem::path& path, unsigned int mode = 0700) const;
    bool exists(const std::filesystem::path& path) const;
    void remove_contents(const std::filesystem::path& directory) const;
    void remove_tree(const std::filesystem::path& path) const;
    void delete_subvolume(const std::filesystem::path& path) const;

private:
    std::filesystem::path relative_path(const std::filesystem::path& path) const;
    SafeDirectoryHandle open_relative(const std::filesystem::path& relative, int flags) const;

    std::filesystem::path root_path_;
    SafeDirectoryHandle root_;
};

} // namespace btrfsbackup
