// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <string>

#include <daemon/ManagerPaths.hpp>

namespace btrfsbackup::daemon {

enum class ManagerChangeKind {
    Profiles,
    Status,
    History,
    Device,
};

struct ManagerChange {
    ManagerChangeKind kind;
    std::string profile_id;
};

class ManagerChangeMonitor final {
  public:
    using Callback = std::function<void(const ManagerChange&)>;

    ManagerChangeMonitor(const ManagerPaths& paths, Callback callback);
    ~ManagerChangeMonitor();

    ManagerChangeMonitor(const ManagerChangeMonitor&) = delete;
    ManagerChangeMonitor& operator=(const ManagerChangeMonitor&) = delete;
    ManagerChangeMonitor(ManagerChangeMonitor&&) = delete;
    ManagerChangeMonitor& operator=(ManagerChangeMonitor&&) = delete;

    [[nodiscard]] int filesystem_fd() const noexcept;
    [[nodiscard]] int device_fd() const noexcept;
    [[nodiscard]] int mount_fd() const noexcept;

    void process_filesystem_events();
    void process_device_events();
    void process_mount_events();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace btrfsbackup::daemon
