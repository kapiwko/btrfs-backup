#pragma once

#include <string>

#include <btrfsbackup/json.hpp>
#include <btrfsbackup/profile.hpp>

namespace btrfsbackup {

std::string render_profile_env(const Profile& profile);
std::string render_profile_env(const Json& profile);
std::string render_source(const ProfileSource& source);
std::string render_source(const Json& source);
std::string render_udev(const Profile& profile);
std::string render_udev(const Json& profile);

} // namespace btrfsbackup
