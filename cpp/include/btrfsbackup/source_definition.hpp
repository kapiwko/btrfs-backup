#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup {

struct SourceDefinition {
    bool enabled = true;
    std::string name;
    std::string subvolume;
    std::string local_snapshot_dir;
    std::string remote_subdir;
    long long remote_retention = 0;
    long long local_retention = 0;
};

SourceDefinition load_source_definition(
    const std::filesystem::path& source_config,
    long long default_remote_retention,
    long long default_local_retention
);

void command_parse_source_definition(const std::vector<std::string>& args, std::ostream& output);

} // namespace btrfsbackup
