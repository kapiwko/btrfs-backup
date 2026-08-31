// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include <daemon/ManagerResponseModels.hpp>
#include <daemon/control/ProfileAdministrationService.hpp>

namespace btrfsbackup::daemon::dbus {

class ManagerJsonCodec {
  public:
    [[nodiscard]] std::string encode(const ManagerCapabilities& capabilities) const;
    [[nodiscard]] std::string encode(const std::vector<ProfileSummary>& profiles) const;
    [[nodiscard]] std::string encode(const PublicStatusResponse& status) const;
    [[nodiscard]] std::string encode(const SanitizedHistoryPage& page) const;
    [[nodiscard]] std::string encode(const TargetStatus& status) const;
    [[nodiscard]] std::string encode(const OperationResult& result) const;
    [[nodiscard]] std::string encode(const BrowseSessionInfo& session) const;
    [[nodiscard]] std::string encode(const std::vector<BackupCoverage>& coverage) const;
    [[nodiscard]] std::string encode(const control::EditableProfile& profile) const;
    [[nodiscard]] std::string encode(const control::ProfileDetails& profile) const;
    [[nodiscard]] std::string encode(const control::ProfileDraftResult& draft) const;
};

} // namespace btrfsbackup::daemon::dbus
