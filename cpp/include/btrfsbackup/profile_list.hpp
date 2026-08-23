#pragma once

#include <filesystem>
#include <iosfwd>

namespace btrfsbackup {

void command_list_profiles(
    const std::filesystem::path& profile_config_dir,
    const std::filesystem::path& profile_root_dir,
    std::ostream& output
);

} // namespace btrfsbackup
