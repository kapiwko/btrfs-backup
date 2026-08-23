#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup::command {

int runner(const std::filesystem::path& profile_config_dir, const std::vector<std::string>& args, std::ostream& output);

} // namespace btrfsbackup::command
