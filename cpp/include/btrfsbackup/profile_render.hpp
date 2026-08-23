#pragma once

#include <string>

#include <btrfsbackup/json.hpp>

namespace btrfsbackup {

std::string render_profile_env(const Json& profile);
std::string render_source(const Json& source);
std::string render_udev(const Json& profile);

} // namespace btrfsbackup
