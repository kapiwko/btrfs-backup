#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup::command {

void status_history(
    const std::filesystem::path& history_root,
    const std::vector<std::string>& args,
    std::ostream& output
);

} // namespace btrfsbackup::command
