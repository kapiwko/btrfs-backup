#pragma once

#include <string>
#include <vector>

namespace btrfsbackup {

struct CommandResult {
    int exit_code = 0;
    std::string output;
};

CommandResult run_command(const std::vector<std::string>& argv);
std::string run_capture(const std::vector<std::string>& argv);

} // namespace btrfsbackup
