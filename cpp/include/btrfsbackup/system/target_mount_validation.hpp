#pragma once

#include <vector>

#include <btrfsbackup/system/mount_info.hpp>
#include <btrfsbackup/model/profile.hpp>

namespace btrfsbackup {

void validate_target_mount(const Profile& profile, const std::vector<MountEntry>& mounts);

} // namespace btrfsbackup
