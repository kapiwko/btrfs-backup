#pragma once

#include <vector>

#include <btrfsbackup/mount_info.hpp>
#include <btrfsbackup/profile.hpp>

namespace btrfsbackup {

void validate_target_mount(const Profile& profile, const std::vector<MountEntry>& mounts);

} // namespace btrfsbackup
