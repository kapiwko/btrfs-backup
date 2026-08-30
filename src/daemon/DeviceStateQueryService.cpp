// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/DeviceStateQueryService.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <config/model/Profile.hpp>
#include <config/model/ProfileDocument.hpp>
#include <core/Identifiers.hpp>
#include <daemon/ManagerDocumentReader.hpp>
#include <platform/linux/DeviceInfo.hpp>
#include <platform/linux/MountInfo.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon {

DeviceStateQueryService::DeviceStateQueryService(ManagerPaths paths)
    : paths_(std::move(paths)) {
}

TargetStatus DeviceStateQueryService::get_device_state(
    const std::string& profile_id
) const {
    validate_profile_id(profile_id);
    const fs::path profile_path = paths_.config_root / "profiles" / profile_id / "profile.json";
    const btrfsbackup::config::Profile profile = btrfsbackup::config::profile_from_json(
        read_manager_json_document(profile_path),
        paths_.target_mount_root
    );
    const fs::path mapper = btrfsbackup::platform::linux::mapper_path(
        profile.target.mapper_name.value(),
        paths_.mapper_root
    );
    const fs::path mountpoint = paths_.target_mount_root / profile_id;
    const std::vector<btrfsbackup::backup::MountEntry> mounts =
        btrfsbackup::platform::linux::read_mount_table(paths_.mountinfo_path);
    const bool mounted = btrfsbackup::backup::mount_at(mounts, mountpoint).has_value();
    const bool unlocked = fs::exists(mapper);
    const bool connected = fs::exists(profile.target.device);

    std::string state = "disconnected";
    bool safe_to_remove = false;
    if (mounted) {
        state = "mounted";
    } else if (unlocked) {
        state = "unlocked";
    } else if (connected) {
        state = "connected";
        safe_to_remove = true;
    }
    return {
        .profile_id = profile_id,
        .target_name = std::string(profile.target.mapper_name.value()),
        .state = state,
        .connected = connected,
        .unlocked = unlocked,
        .mounted = mounted,
        .safe_to_remove = safe_to_remove,
    };
}

} // namespace btrfsbackup::daemon
