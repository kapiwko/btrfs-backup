#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup::command {

bool status_watch_once(
    const std::filesystem::path& status_root,
    const std::vector<std::string>& args,
    std::string& previous,
    std::ostream& output
);

int status(
    const std::filesystem::path& status_root,
    const std::filesystem::path& history_root,
    const std::vector<std::string>& args
);

} // namespace btrfsbackup::command
