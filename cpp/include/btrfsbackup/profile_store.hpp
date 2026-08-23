#pragma once

#include <filesystem>

#include <btrfsbackup/json.hpp>

namespace btrfsbackup {

void render_tree(const Json& profile, const std::filesystem::path& output_dir);
void save_tree(
    const Json& profile,
    const std::filesystem::path& etc_root,
    const std::filesystem::path& udev_root,
    const std::filesystem::path& public_root
);

} // namespace btrfsbackup
