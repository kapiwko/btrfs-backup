// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/ApplicationConfig.hpp>

#include <utility>

namespace btrfsbackup::config {

ApplicationConfig::ApplicationConfig() : ApplicationConfig(defaults().paths()) {
}

ApplicationConfig::ApplicationConfig(ApplicationPaths paths) : paths_(std::move(paths)) {
}

ApplicationConfig ApplicationConfig::defaults() {
    return ApplicationConfig({
        .state_root = "/var/lib/btrfs-backup",
        .status_root = "/run/btrfs-backup/profiles",
        .history_root = "/var/lib/btrfs-backup/history",
        .target_mount_root = "/mnt/btrfs-backup",
    });
}

const ApplicationPaths& ApplicationConfig::paths() const {
    return paths_;
}

} // namespace btrfsbackup::config
