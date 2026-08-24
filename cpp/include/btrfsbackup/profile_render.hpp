#pragma once

#include <string>

#include <btrfsbackup/profile.hpp>

namespace btrfsbackup {

std::string render_udev(const Profile& profile);

} // namespace btrfsbackup
