#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

#include <btrfsbackup/mount_info.hpp>
#include <btrfsbackup/runtime_adapters.hpp>

namespace btrfsbackup::command {

struct TargetExecutionServices {
    ICommandRunner& commands;
    std::function<std::vector<MountEntry>()> read_mounts;
};

int target(
    const std::filesystem::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output
);

int target(
    const std::filesystem::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    TargetExecutionServices* services
);

} // namespace btrfsbackup::command
