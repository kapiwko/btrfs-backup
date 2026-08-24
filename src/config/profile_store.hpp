#pragma once

#include <filesystem>
#include <functional>

#include <config/profile.hpp>

namespace btrfsbackup {

void render_tree(const Profile& profile, const std::filesystem::path& output_dir);
void save_tree(
    const Profile& profile,
    const std::filesystem::path& etc_root,
    const std::filesystem::path& udev_root,
    const std::filesystem::path& systemd_root,
    const std::filesystem::path& public_root,
    const std::function<void()>& activate = {}
);

} // namespace btrfsbackup
