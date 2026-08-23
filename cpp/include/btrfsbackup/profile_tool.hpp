#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace btrfsbackup {

int command_profile(
    const std::vector<std::string>& args,
    const std::filesystem::path& profile_config_dir = "/etc/btrfs-backup/profiles.d"
);

} // namespace btrfsbackup
