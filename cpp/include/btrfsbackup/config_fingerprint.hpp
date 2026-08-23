#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup {

std::string compute_config_fingerprint(
    const std::string& version,
    const std::filesystem::path& config_file,
    const std::vector<std::filesystem::path>& source_files
);

void command_config_fingerprint(const std::vector<std::string>& args, std::ostream& output);

} // namespace btrfsbackup
