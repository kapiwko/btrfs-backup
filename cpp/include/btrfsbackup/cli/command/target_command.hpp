#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include <btrfsbackup/application/target_service.hpp>

namespace btrfsbackup::command {

using TargetExecutionServices = TargetServiceDependencies;

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
