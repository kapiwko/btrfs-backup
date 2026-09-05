// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <systemd/sd-bus.h>

#include <daemon/dbus/ManagerMethodSupport.hpp>

namespace btrfsbackup::daemon {
class ManagerService;
namespace control {
class BrowseSessionService;
}
} // namespace btrfsbackup::daemon

namespace btrfsbackup::daemon::dbus {

class ManagerBrowseMethods final {
  public:
    ManagerBrowseMethods(
        ManagerService& service,
        control::BrowseSessionService& browse_sessions,
        ManagerMethodSupport& support
    );

    int open_browse_session(sd_bus_message* message, sd_bus_error* error) noexcept;
    int renew_browse_session(sd_bus_message* message, sd_bus_error* error) noexcept;
    int set_browse_session_active(sd_bus_message* message, sd_bus_error* error) noexcept;
    int close_browse_session(sd_bus_message* message, sd_bus_error* error) noexcept;
    int list_browse_directory(sd_bus_message* message, sd_bus_error* error) noexcept;
    int list_browse_directory_page(sd_bus_message* message, sd_bus_error* error) noexcept;
    int inspect_browse_entry(sd_bus_message* message, sd_bus_error* error) noexcept;
    int open_browse_file(sd_bus_message* message, sd_bus_error* error) noexcept;
    int open_browse_root(sd_bus_message* message, sd_bus_error* error) noexcept;
    int inspect_browse_repository(sd_bus_message* message, sd_bus_error* error) noexcept;
    int resolve_backup_coverage(sd_bus_message* message, sd_bus_error* error) noexcept;

  private:
    ManagerService& service_;
    control::BrowseSessionService& browse_sessions_;
    ManagerMethodSupport& support_;
};

} // namespace btrfsbackup::daemon::dbus
