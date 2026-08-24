#pragma once

#include <string>
#include <vector>

namespace btrfsbackup::command {

int profile_create(const std::vector<std::string>& args);

} // namespace btrfsbackup::command
