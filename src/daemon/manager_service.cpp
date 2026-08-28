// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/manager_service.hpp>

#include <utility>

namespace btrfsbackup::daemon {

ManagerService::ManagerService(ManagerPaths paths)
    : profiles_(paths.public_profile_root),
      history_(paths.history_root),
      status_(paths.status_root, history_),
      device_state_(std::move(paths)) {
}

ManagerCapabilities ManagerService::get_capabilities() const {
    return {
        .interface_name = "io.github.btrfsbackup.Manager1",
        .read_only = false,
        .features = {
            "profiles",
            "status",
            "sanitized-history",
            "device-state",
            "start-backup",
            "cancel-backup",
            "validate-target",
            "eject-target",
        },
    };
}

std::vector<ProfileSummary> ManagerService::list_profiles() const {
    return profiles_.list_profiles();
}

PublicRunStatus ManagerService::get_status(const std::string& profile_id) const {
    return status_.get_status(profile_id);
}

SanitizedHistoryPage ManagerService::get_history_sanitized(
    const std::string& profile_id,
    std::size_t offset,
    std::size_t limit
) const {
    return history_.get_history_sanitized(profile_id, offset, limit);
}

TargetStatus ManagerService::get_device_state(const std::string& profile_id) const {
    return device_state_.get_device_state(profile_id);
}

} // namespace btrfsbackup::daemon
