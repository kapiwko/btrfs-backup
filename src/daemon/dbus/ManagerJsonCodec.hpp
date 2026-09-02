// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include <daemon/ManagerResponseModels.hpp>
#include <daemon/control/ProfileAdministrationService.hpp>
#include <daemon/control/CredentialAdministrationService.hpp>
#include <daemon/control/DeviceProvisioningService.hpp>
#include <daemon/provisioning/DevicePreparationPlan.hpp>
#include <daemon/provisioning/StorageTopology.hpp>

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
    [[nodiscard]] std::string encode(const control::ProfileDetails& profile) const;
    [[nodiscard]] std::string encode(const std::vector<control::TargetCredential>& credentials) const;
    [[nodiscard]] std::string encode(const provisioning::StorageTopology& topology) const;
    [[nodiscard]] std::string encode(const provisioning::ExistingTargetInspection& inspection) const;
    [[nodiscard]] std::string encode(const provisioning::DevicePreparationPlan& plan) const;
    [[nodiscard]] std::string encode(const control::DevicePreparationStatus& status) const;
};

} // namespace btrfsbackup::daemon::dbus
