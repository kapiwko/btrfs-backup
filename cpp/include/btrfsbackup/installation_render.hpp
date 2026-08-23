#pragma once

#include <filesystem>
#include <string>

#include <btrfsbackup/profile.hpp>

namespace btrfsbackup {

struct InstallationRenderOptions {
    std::string backup_script = "/usr/lib/btrfs-backup/btrfs-backup.sh";
    std::string eject_script = "/usr/lib/btrfs-backup/btrfs-backup-eject.sh";
    std::string keyfile = "none";
};

void render_installation_files(
    const Profile& profile,
    const std::filesystem::path& output_dir,
    const InstallationRenderOptions& options
);

} // namespace btrfsbackup
