#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup::command {

void status_show(
    const std::filesystem::path& status_root,
    const std::filesystem::path& history_root,
    const std::vector<std::string>& args,
    std::ostream& output
);

} // namespace btrfsbackup::command
