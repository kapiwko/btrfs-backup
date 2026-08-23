#pragma once

#include <string>
#include <vector>

namespace btrfsbackup::command {

int create_profile(const std::vector<std::string>& args);

} // namespace btrfsbackup::command
