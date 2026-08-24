#pragma once

#include <filesystem>

#include <btrfsbackup/system/application_paths.hpp>

namespace btrfsbackup {

class ApplicationConfig {
public:
    ApplicationConfig();
    explicit ApplicationConfig(ApplicationPaths paths);

    static ApplicationConfig defaults(const std::filesystem::path& config_root = "/etc/btrfs-backup");
    static ApplicationConfig load(const std::filesystem::path& config_root = "/etc/btrfs-backup");

    const ApplicationPaths& paths() const;

private:
    ApplicationPaths paths_;
};

} // namespace btrfsbackup
