#include <cli/profile_list_command.hpp>

#include <filesystem>
#include <ostream>
#include <string>

#include <config/profile_service.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::command {

void profile_list(
    const fs::path&,
    const fs::path& profile_root_dir,
    std::ostream& output
) {
    for (const std::string& profile : list_profiles(profile_root_dir)) {
        output << profile << '\n';
    }
}

} // namespace btrfsbackup::command
