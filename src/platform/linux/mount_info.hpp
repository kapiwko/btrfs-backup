#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace btrfsbackup {

struct MountEntry {
    std::string source;
    std::string target;
    std::string fstype;
    std::string root;
    std::string options;
    std::string device_id;
    std::string filesystem_uuid;
};

using FilesystemUuidResolver = std::function<std::string(const std::string&)>;

std::string blkid_filesystem_uuid(const std::string& source);
std::vector<MountEntry> read_mount_table(const std::filesystem::path& mountinfo_path = "/proc/self/mountinfo");
std::vector<MountEntry> read_mount_table(const std::filesystem::path& mountinfo_path, const FilesystemUuidResolver& filesystem_uuid_resolver);
std::vector<std::string> btrfs_mount_targets(const std::filesystem::path& mountinfo_path = "/proc/self/mountinfo");
std::optional<MountEntry> mount_at(const std::vector<MountEntry>& entries, const std::filesystem::path& target);
std::optional<MountEntry> mount_for_path(const std::vector<MountEntry>& entries, const std::filesystem::path& path);
bool paths_are_same_filesystem(const std::vector<MountEntry>& entries, const std::filesystem::path& path_a, const std::filesystem::path& path_b);
bool mount_uses_mapper(const std::vector<MountEntry>& entries, const std::filesystem::path& mountpoint, const std::filesystem::path& mapper_path);

} // namespace btrfsbackup
