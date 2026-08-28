// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <config/model/json.hpp>
#include <daemon/manager_paths.hpp>

namespace btrfsbackup::daemon {

class DeviceStateQueryService {
  public:
    explicit DeviceStateQueryService(ManagerPaths paths);

    [[nodiscard]] btrfsbackup::config::Json get_device_state(
        const std::string& profile_id
    ) const;

  private:
    ManagerPaths paths_;
};

} // namespace btrfsbackup::daemon
