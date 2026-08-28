// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <utility>

#include <backup/ports/safe_directory.hpp>

namespace test_support {

class FakeSafeDirectoryHandle final : public btrfsbackup::backup::ISafeDirectoryHandle {
  public:
    explicit FakeSafeDirectoryHandle(std::filesystem::path path) : path_(std::move(path)) {
    }

    [[nodiscard]] std::filesystem::path stable_path() const override {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

class FakeSafeDirectoryRoot final : public btrfsbackup::backup::ISafeDirectoryRoot {
  public:
    FakeSafeDirectoryRoot(
        std::filesystem::path root,
        std::filesystem::path stable_prefix,
        bool create_directories
    )
        : root_(std::move(root)),
          stable_prefix_(std::move(stable_prefix)),
          create_directories_(create_directories) {
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept override {
        return root_;
    }

    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::ISafeDirectoryHandle> pin_directory(
        const std::filesystem::path& path
    ) const override {
        return std::make_unique<FakeSafeDirectoryHandle>(stable_path(path));
    }

    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::ISafeDirectoryHandle> pin_path(
        const std::filesystem::path& path
    ) const override {
        return std::make_unique<FakeSafeDirectoryHandle>(stable_path(path));
    }

    void ensure_directory(const std::filesystem::path& path, unsigned int) const override {
        if (create_directories_) {
            std::filesystem::create_directories(path);
        }
    }

    [[nodiscard]] bool exists(const std::filesystem::path& path) const override {
        return std::filesystem::exists(path);
    }

    void remove_contents(const std::filesystem::path& directory) const override {
        if (!std::filesystem::exists(directory)) {
            return;
        }
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory)) {
            std::filesystem::remove_all(entry.path());
        }
    }

    void remove_tree(const std::filesystem::path& path) const override {
        std::filesystem::remove_all(path);
    }

    void delete_subvolume(const std::filesystem::path& path) const override {
        std::filesystem::remove_all(path);
    }

  private:
    [[nodiscard]] std::filesystem::path stable_path(const std::filesystem::path& path) const {
        return stable_prefix_.empty() ? path : stable_prefix_ / path.relative_path();
    }

    std::filesystem::path root_;
    std::filesystem::path stable_prefix_;
    bool create_directories_ = false;
};

class FakeSafeDirectoryRootFactory final : public btrfsbackup::backup::ISafeDirectoryRootFactory {
  public:
    explicit FakeSafeDirectoryRootFactory(
        std::filesystem::path stable_prefix = {},
        bool create_directories = false
    )
        : stable_prefix_(std::move(stable_prefix)),
          create_directories_(create_directories) {
    }

    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::ISafeDirectoryRoot> open(
        const std::filesystem::path& root
    ) const override {
        return std::make_unique<FakeSafeDirectoryRoot>(root, stable_prefix_, create_directories_);
    }

  private:
    std::filesystem::path stable_prefix_;
    bool create_directories_ = false;
};

} // namespace test_support
