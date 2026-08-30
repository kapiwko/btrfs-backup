// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <config/model/TargetIdentity.hpp>
#include <core/Identifiers.hpp>

namespace btrfsbackup::platform::linux {

std::filesystem::path default_lock_root();
std::filesystem::path profile_lock_path(const std::filesystem::path& lock_root, const ProfileId& profile_id);
std::filesystem::path target_lock_path(const std::filesystem::path& lock_root, const btrfsbackup::config::LuksUuid& luks_uuid);

class FileLock {
  public:
    explicit FileLock(std::filesystem::path path);
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;
    ~FileLock() noexcept;

    [[nodiscard]] bool try_acquire();
    void release();
    [[nodiscard]] bool acquired() const noexcept;

  private:
    std::filesystem::path path_;
    int fd_ = -1;
    bool acquired_ = false;
};

} // namespace btrfsbackup::platform::linux
