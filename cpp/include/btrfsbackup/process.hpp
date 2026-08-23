#pragma once

#include <string>
#include <vector>

namespace btrfsbackup {

std::string run_capture(const std::vector<std::string>& argv);

} // namespace btrfsbackup
