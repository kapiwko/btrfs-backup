#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup::command {

void state_fingerprint(const std::vector<std::string>& args, std::ostream& output);

} // namespace btrfsbackup::command
