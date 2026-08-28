// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include <daemon/manager_response_models.hpp>

namespace btrfsbackup::daemon {

class ManagerJsonCodec {
  public:
    [[nodiscard]] std::string encode(const ManagerCapabilities& capabilities) const;
    [[nodiscard]] std::string encode(const std::vector<ProfileSummary>& profiles) const;
    [[nodiscard]] std::string encode(const PublicRunStatus& status) const;
    [[nodiscard]] std::string encode(const SanitizedHistoryPage& page) const;
    [[nodiscard]] std::string encode(const TargetStatus& status) const;
    [[nodiscard]] std::string encode(const OperationResult& result) const;
};

} // namespace btrfsbackup::daemon
