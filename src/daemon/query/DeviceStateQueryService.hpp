// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <daemon/ManagerPaths.hpp>
#include <daemon/ManagerResponseModels.hpp>

namespace btrfsbackup::daemon::query {

class DeviceStateQueryService {
  public:
    explicit DeviceStateQueryService(ManagerPaths paths);

    [[nodiscard]] TargetStatus get_device_state(
        const std::string& profile_id
    ) const;

  private:
    ManagerPaths paths_;
};

} // namespace btrfsbackup::daemon::query
