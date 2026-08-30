// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/ManagerService.hpp>

#include <utility>

#include <core/ManagerProtocol.hpp>

namespace btrfsbackup::daemon {

ManagerService::ManagerService(ManagerPaths paths)
    : profiles_(paths.public_profile_root),
      history_(paths.history_root),
      status_(paths.status_root, paths.state_root, history_),
      device_state_(std::move(paths)) {
}

ManagerCapabilities ManagerService::get_capabilities() const {
    return {
        .interface_name = manager_protocol::interface_name,
        .read_only = false,
        .features = {
            manager_protocol::feature::profiles,
            manager_protocol::feature::status,
            manager_protocol::feature::sanitized_history,
            manager_protocol::feature::device_state,
            manager_protocol::feature::target_storage_usage,
            manager_protocol::feature::start_backup,
            manager_protocol::feature::cancel_backup,
            manager_protocol::feature::validate_target,
            manager_protocol::feature::eject_target,
            manager_protocol::feature::change_signals,
            manager_protocol::feature::profile_administration,
            manager_protocol::feature::profile_hook_administration,
            manager_protocol::feature::browse_backups,
        },
    };
}

std::vector<ProfileSummary> ManagerService::list_profiles() const {
    return profiles_.list_profiles();
}

PublicStatusResponse ManagerService::get_status(const std::string& profile_id) const {
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
