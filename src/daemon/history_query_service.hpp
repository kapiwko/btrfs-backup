// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#include <daemon/manager_response_models.hpp>

namespace btrfsbackup::daemon {

class HistoryQueryService {
  public:
    explicit HistoryQueryService(std::filesystem::path history_root);

    [[nodiscard]] SanitizedHistoryPage get_history_sanitized(
        const std::string& profile_id,
        std::size_t offset,
        std::size_t limit
    ) const;
    [[nodiscard]] std::optional<SanitizedHistoryEntry> get_last_sanitized(
        const std::string& profile_id
    ) const;

  private:
    std::filesystem::path history_root_;
};

} // namespace btrfsbackup::daemon
