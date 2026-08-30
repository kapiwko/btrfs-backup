// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>

namespace btrfsbackup::platform::linux::filesystem {

class InotifyFileChangeWatcher final {
  public:
    explicit InotifyFileChangeWatcher(std::filesystem::path path);
    ~InotifyFileChangeWatcher() noexcept;

    InotifyFileChangeWatcher(const InotifyFileChangeWatcher&) = delete;
    InotifyFileChangeWatcher& operator=(const InotifyFileChangeWatcher&) = delete;
    InotifyFileChangeWatcher(InotifyFileChangeWatcher&&) = delete;
    InotifyFileChangeWatcher& operator=(InotifyFileChangeWatcher&&) = delete;

    void wait_for_change(
        std::optional<std::chrono::milliseconds> resync_timeout = std::nullopt
    );

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace btrfsbackup::platform::linux::filesystem
