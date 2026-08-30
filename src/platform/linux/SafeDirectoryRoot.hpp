// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <backup/ports/SafeDirectory.hpp>
#include <backup/transfer/TransferPlan.hpp>

namespace btrfsbackup::platform::linux {

class SafeDirectoryHandle final : public btrfsbackup::backup::ISafeDirectoryHandle {
  public:
    SafeDirectoryHandle() = default;
    explicit SafeDirectoryHandle(int fd);
    SafeDirectoryHandle(const SafeDirectoryHandle&) = delete;
    SafeDirectoryHandle& operator=(const SafeDirectoryHandle&) = delete;
    SafeDirectoryHandle(SafeDirectoryHandle&& other) noexcept;
    SafeDirectoryHandle& operator=(SafeDirectoryHandle&& other) noexcept;
    ~SafeDirectoryHandle() noexcept override;

    int fd() const noexcept;
    std::filesystem::path proc_path() const;
    [[nodiscard]] std::filesystem::path stable_path() const override;

  private:
    int fd_ = -1;
};

class SafeDirectoryRoot final : public btrfsbackup::backup::ISafeDirectoryRoot {
  public:
    explicit SafeDirectoryRoot(const std::filesystem::path& root);
    SafeDirectoryRoot(const SafeDirectoryRoot&) = delete;
    SafeDirectoryRoot& operator=(const SafeDirectoryRoot&) = delete;
    SafeDirectoryRoot(SafeDirectoryRoot&&) noexcept = default;
    SafeDirectoryRoot& operator=(SafeDirectoryRoot&&) noexcept = default;

    const std::filesystem::path& path() const noexcept override;
    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::ISafeDirectoryHandle> pin_directory(
        const std::filesystem::path& path
    ) const override;
    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::ISafeDirectoryHandle> pin_path(
        const std::filesystem::path& path
    ) const override;
    SafeDirectoryHandle open_directory(const std::filesystem::path& path) const;
    SafeDirectoryHandle open_path(const std::filesystem::path& path) const;
    void ensure_directory(const std::filesystem::path& path, unsigned int mode = 0700) const override;
    bool exists(const std::filesystem::path& path) const override;
    void remove_contents(const std::filesystem::path& directory) const override;
    void remove_tree(const std::filesystem::path& path) const override;
    void delete_subvolume(const std::filesystem::path& path) const override;

  private:
    std::filesystem::path relative_path(const std::filesystem::path& path) const;
    SafeDirectoryHandle open_relative(const std::filesystem::path& relative, int flags) const;

    std::filesystem::path root_path_;
    SafeDirectoryHandle root_;
};

class SafeDirectoryRootFactory final : public btrfsbackup::backup::ISafeDirectoryRootFactory {
  public:
    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::ISafeDirectoryRoot> open(
        const std::filesystem::path& root
    ) const override;
};

} // namespace btrfsbackup::platform::linux
