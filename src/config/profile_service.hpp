#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <config/profile.hpp>

namespace btrfsbackup {

struct ProfileInstallationRoots {
    std::filesystem::path etc_root;
    std::filesystem::path udev_root;
    std::filesystem::path systemd_root;
    std::filesystem::path public_root;
};

Profile validate_profile_file(const std::filesystem::path& file);
void write_profile_file(const Profile& profile, const std::filesystem::path& output);
void render_profile(const std::filesystem::path& file, const std::filesystem::path& output_dir);
Profile save_profile(const std::filesystem::path& file, const ProfileInstallationRoots& roots);
Profile get_profile(const std::filesystem::path& etc_root, const std::string& profile_id);
Profile export_profile(
    const std::filesystem::path& etc_root,
    const std::string& profile_id,
    const std::filesystem::path& output
);
std::vector<std::string> list_profiles(const std::filesystem::path& profile_root);

} // namespace btrfsbackup
