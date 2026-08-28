// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <daemon/manager_response_models.hpp>

namespace btrfsbackup::daemon {

class HistoryQueryService;

class StatusQueryService {
  public:
    StatusQueryService(
        std::filesystem::path status_root,
        const HistoryQueryService& history
    );

    [[nodiscard]] PublicRunStatus get_status(const std::string& profile_id) const;

  private:
    std::filesystem::path status_root_;
    const HistoryQueryService& history_;
};

} // namespace btrfsbackup::daemon
