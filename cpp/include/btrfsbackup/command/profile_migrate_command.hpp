#pragma once

#include <string>
#include <vector>

namespace btrfsbackup::command {

int profile_migrate(const std::vector<std::string>& args);

} // namespace btrfsbackup::command
