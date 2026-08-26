// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

namespace btrfsbackup {

class ISafeDirectoryHandle {
  public:
    virtual ~ISafeDirectoryHandle() = default;

    [[nodiscard]] virtual std::filesystem::path stable_path() const = 0;
};

class ISafeDirectoryRoot {
  public:
    virtual ~ISafeDirectoryRoot() = default;

    [[nodiscard]] virtual const std::filesystem::path& path() const noexcept = 0;
    [[nodiscard]] virtual std::unique_ptr<ISafeDirectoryHandle> pin_directory(
        const std::filesystem::path& path
    ) const = 0;
    [[nodiscard]] virtual std::unique_ptr<ISafeDirectoryHandle> pin_path(
        const std::filesystem::path& path
    ) const = 0;
    virtual void ensure_directory(const std::filesystem::path& path, unsigned int mode = 0700) const = 0;
    [[nodiscard]] virtual bool exists(const std::filesystem::path& path) const = 0;
    virtual void remove_contents(const std::filesystem::path& directory) const = 0;
    virtual void remove_tree(const std::filesystem::path& path) const = 0;
    virtual void delete_subvolume(const std::filesystem::path& path) const = 0;
};

} // namespace btrfsbackup
