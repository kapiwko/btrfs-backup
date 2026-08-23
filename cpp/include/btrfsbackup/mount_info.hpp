#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace btrfsbackup {

struct MountEntry {
    std::string source;
    std::string target;
    std::string fstype;
};

std::vector<MountEntry> read_mount_table(const std::filesystem::path& mountinfo_path = "/proc/self/mountinfo");
std::vector<std::string> btrfs_mount_targets(const std::filesystem::path& mountinfo_path = "/proc/self/mountinfo");

} // namespace btrfsbackup
