#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup {

void command_parse_profile_sources(const std::vector<std::string>& args, std::ostream& output);

} // namespace btrfsbackup
