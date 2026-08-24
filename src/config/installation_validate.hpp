#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup {

void validate_rendered_installation(
    const std::filesystem::path& root,
    const std::filesystem::path& target_mount_root = "/mnt/btrfs-backup"
);
void validate_active_installation(const std::string& profile_id);

} // namespace btrfsbackup
