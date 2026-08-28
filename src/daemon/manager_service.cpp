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

btrfsbackup::config::Json ManagerService::get_capabilities() const {
    return {
        {"schemaVersion", 1},
        {"interface", "io.github.btrfsbackup.Manager1"},
        {"apiMajor", 1},
        {"apiMinor", 0},
        {"profileSchemaVersion", 3},
        {"publicStatusSchemaVersion", 3},
        {"historySchemaVersion", 1},
        {"deviceStateSchemaVersion", 1},
        {"readOnly", true},
        {"features", btrfsbackup::config::Json::array({"profiles", "status", "sanitized-history", "device-state"})},
    };
}

btrfsbackup::config::Json ManagerService::list_profiles() const {
    return profiles_.list_profiles();
}

btrfsbackup::config::Json ManagerService::get_status(const std::string& profile_id) const {
    return status_.get_status(profile_id);
}

btrfsbackup::config::Json ManagerService::get_history_sanitized(
    const std::string& profile_id,
    std::size_t offset,
    std::size_t limit
) const {
    return history_.get_history_sanitized(profile_id, offset, limit);
}

btrfsbackup::config::Json ManagerService::get_device_state(const std::string& profile_id) const {
    return device_state_.get_device_state(profile_id);
}

} // namespace btrfsbackup::daemon
