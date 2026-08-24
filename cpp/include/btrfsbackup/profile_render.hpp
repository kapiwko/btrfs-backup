#pragma once

#include <string>

#include <btrfsbackup/profile.hpp>

namespace btrfsbackup {

std::string render_udev(const Profile& profile);
std::string render_mount_requirement(const Profile& profile);
std::string render_mount_dependency(const Profile& profile);

} // namespace btrfsbackup
