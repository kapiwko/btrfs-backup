#pragma once

#include <vector>

#include <platform/linux/mount_info.hpp>
#include <config/profile.hpp>

namespace btrfsbackup {

void validate_target_mount(const Profile& profile, const std::vector<MountEntry>& mounts);

} // namespace btrfsbackup
