#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace btrfsbackup::command {

void write_status(
    const std::filesystem::path& status_root,
    const std::filesystem::path& history_root,
    const std::vector<std::string>& args
);

} // namespace btrfsbackup::command
