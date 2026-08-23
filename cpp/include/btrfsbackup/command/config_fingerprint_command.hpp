#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup::command {

void config_fingerprint(const std::vector<std::string>& args, std::ostream& output);

} // namespace btrfsbackup::command
