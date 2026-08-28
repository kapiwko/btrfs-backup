// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>

#include <config/model/json.hpp>
#include <daemon/device_state_query_service.hpp>
#include <daemon/history_query_service.hpp>
#include <daemon/manager_paths.hpp>
#include <daemon/profile_query_service.hpp>
#include <daemon/status_query_service.hpp>

namespace btrfsbackup::daemon {

class ManagerService {
  public:
    explicit ManagerService(ManagerPaths paths);
    ManagerService(const ManagerService&) = delete;
    ManagerService& operator=(const ManagerService&) = delete;
    ManagerService(ManagerService&&) = delete;
    ManagerService& operator=(ManagerService&&) = delete;

    [[nodiscard]] btrfsbackup::config::Json get_capabilities() const;
    [[nodiscard]] btrfsbackup::config::Json list_profiles() const;
    [[nodiscard]] btrfsbackup::config::Json get_status(const std::string& profile_id) const;
    [[nodiscard]] btrfsbackup::config::Json get_history_sanitized(
        const std::string& profile_id,
        std::size_t offset,
        std::size_t limit
    ) const;
    [[nodiscard]] btrfsbackup::config::Json get_device_state(const std::string& profile_id) const;

  private:
    ProfileQueryService profiles_;
    HistoryQueryService history_;
    StatusQueryService status_;
    DeviceStateQueryService device_state_;
};

} // namespace btrfsbackup::daemon
