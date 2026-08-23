#include <btrfsbackup/target_mount_validation.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/validation.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool has_mount_option(const std::string& options, const std::string& option) {
    return ("," + options + ",").find("," + option + ",") != std::string::npos;
}

fs::path resolved_path(const fs::path& path) {
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(path, ec);
    if (ec) {
        return normalized_path(path);
    }
    return normalized_path(resolved);
}

bool resolved_path_is_within(const fs::path& candidate, const fs::path& base) {
    return path_is_within(resolved_path(candidate), resolved_path(base));
}

} // namespace

void validate_target_mount(const Profile& profile, const std::vector<MountEntry>& mounts) {
    std::optional<MountEntry> target_mount = mount_at(mounts, profile.target.mount_point);
    if (!target_mount.has_value()) {
        throw ValidationError("Backup target is not mounted at " + profile.target.mount_point);
    }

    if (target_mount->fstype != "btrfs") {
        throw ValidationError("Backup target is not a Btrfs filesystem: " + profile.target.mount_point);
    }

    fs::path mapper_path = fs::path("/dev/mapper") / profile.target.mapper_name;
    if (!mount_uses_mapper(mounts, profile.target.mount_point, mapper_path)) {
        throw ValidationError("The filesystem mounted at " + profile.target.mount_point + " is not " + mapper_path.string());
    }

    if (!has_mount_option(target_mount->options, "rw")) {
        throw ValidationError("Backup target is not mounted read-write: " + profile.target.mount_point);
    }

    if (profile.target.btrfs_uuid.empty()) {
        throw ValidationError("target.btrfsUuid is required for target mount validation");
    }
    if (target_mount->filesystem_uuid.empty() || lower(target_mount->filesystem_uuid) != lower(profile.target.btrfs_uuid)) {
        throw ValidationError("Btrfs UUID mismatch at " + profile.target.mount_point);
    }

    if (!resolved_path_is_within(profile.paths.remote_root, profile.target.mount_point)) {
        throw ValidationError("REMOTE_ROOT escapes the backup mountpoint: " + profile.paths.remote_root);
    }
    if (!resolved_path_is_within(profile.paths.incoming_root, profile.target.mount_point)) {
        throw ValidationError("INCOMING_ROOT escapes the backup mountpoint: " + profile.paths.incoming_root);
    }
}

} // namespace btrfsbackup
