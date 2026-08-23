#pragma once

#include <vector>

#include <btrfsbackup/json.hpp>
#include <btrfsbackup/profile.hpp>

namespace btrfsbackup {

std::vector<ProfileSource> profile_sources_from_json(const Json& root);

} // namespace btrfsbackup
