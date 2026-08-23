#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup {

void command_history(
    const std::filesystem::path& history_root,
    const std::vector<std::string>& args,
    std::ostream& output
);

} // namespace btrfsbackup
