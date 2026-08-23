#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

namespace btrfsbackup {

struct ProfileMigrationOptions {
    std::filesystem::path source_config;
    std::filesystem::path profile_config_dir;
    std::filesystem::path source_config_dir;
    std::filesystem::path udev_rules_dir;
    std::filesystem::path public_profile_dir;
    std::string profile_id;
    std::string profile_name;
    bool force = false;
    bool dry_run = false;
    bool remove_legacy = false;
};

bool profile_migration_requires_root(const ProfileMigrationOptions& options);
void execute_profile_migration(const ProfileMigrationOptions& options, std::ostream& output);

} // namespace btrfsbackup
