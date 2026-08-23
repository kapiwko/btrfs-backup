#pragma once

#include <filesystem>
#include <iosfwd>

namespace btrfsbackup {

void command_list_profiles(
    const std::filesystem::path& profile_config_dir,
    const std::filesystem::path& legacy_config_file,
    std::ostream& output
);

} // namespace btrfsbackup
