// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <daemon/ManagerResponseModels.hpp>

namespace btrfsbackup::daemon::query {

class HistoryQueryService;

class StatusQueryService {
  public:
    StatusQueryService(
        std::filesystem::path status_root,
        std::filesystem::path state_root,
        const HistoryQueryService& history
    );

    [[nodiscard]] PublicStatusResponse get_status(const std::string& profile_id) const;

  private:
    std::filesystem::path status_root_;
    std::filesystem::path state_root_;
    const HistoryQueryService& history_;
};

} // namespace btrfsbackup::daemon::query
