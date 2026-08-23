#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup {

std::filesystem::path mapper_path(const std::string& mapper_name, const std::filesystem::path& mapper_root = "/dev/mapper");
std::filesystem::path canonical_device(const std::filesystem::path& path);
std::string strip_subvolume_suffix(const std::string& source);

} // namespace btrfsbackup
