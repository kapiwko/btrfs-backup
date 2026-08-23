#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace btrfsbackup {

std::string compute_config_fingerprint(
    const std::string& version,
    const std::filesystem::path& config_file,
    const std::vector<std::filesystem::path>& source_files
);

} // namespace btrfsbackup
