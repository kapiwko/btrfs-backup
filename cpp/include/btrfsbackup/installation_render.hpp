#pragma once

#include <filesystem>
#include <string>

#include <btrfsbackup/profile.hpp>

namespace btrfsbackup {

struct InstallationRenderOptions {
    std::string backup_command = "/usr/bin/btrfs-backupctl runner execute";
    std::string eject_script = "/usr/bin/btrfs-backup-eject";
    std::string keyfile = "none";
};

void render_installation_files(
    const Profile& profile,
    const std::filesystem::path& output_dir,
    const InstallationRenderOptions& options
);

} // namespace btrfsbackup
