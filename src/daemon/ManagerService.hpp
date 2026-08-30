// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>

#include <daemon/DeviceStateQueryService.hpp>
#include <daemon/HistoryQueryService.hpp>
#include <daemon/ManagerPaths.hpp>
#include <daemon/ProfileQueryService.hpp>
#include <daemon/StatusQueryService.hpp>

namespace btrfsbackup::daemon {

class ManagerService {
  public:
    explicit ManagerService(ManagerPaths paths);
    ManagerService(const ManagerService&) = delete;
    ManagerService& operator=(const ManagerService&) = delete;
    ManagerService(ManagerService&&) = delete;
    ManagerService& operator=(ManagerService&&) = delete;

    [[nodiscard]] ManagerCapabilities get_capabilities() const;
    [[nodiscard]] std::vector<ProfileSummary> list_profiles() const;
    [[nodiscard]] PublicRunStatus get_status(const std::string& profile_id) const;
    [[nodiscard]] SanitizedHistoryPage get_history_sanitized(
        const std::string& profile_id,
        std::size_t offset,
        std::size_t limit
    ) const;
    [[nodiscard]] TargetStatus get_device_state(const std::string& profile_id) const;

  private:
    ProfileQueryService profiles_;
    HistoryQueryService history_;
    StatusQueryService status_;
    DeviceStateQueryService device_state_;
};

} // namespace btrfsbackup::daemon
