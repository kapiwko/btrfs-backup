#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace btrfsbackup {

std::map<std::string, std::string> read_shell_environment(const std::filesystem::path& path);

} // namespace btrfsbackup
