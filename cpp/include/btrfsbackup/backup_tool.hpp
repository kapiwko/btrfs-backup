#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

#include <btrfsbackup/profile.hpp>

namespace btrfsbackup {

struct BackupToolServices {
    std::function<int(const std::vector<std::string>&, std::ostream&)> runner;
    std::function<int(const std::vector<std::string>&, std::ostream&)> target;
    std::function<Profile(const std::string&)> load_profile;
    std::function<bool()> is_service_invocation;
};

int backup_tool(
    const std::filesystem::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    BackupToolServices* services = nullptr
);

int backup_tool_main(int argc, char** argv);

} // namespace btrfsbackup
