// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/ProvisioningSource.hpp>

#include <map>
#include <string_view>

#include <config/domain/Validation.hpp>
#include <core/Errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

bool has_mount_option(std::string_view options, std::string_view expected) {
    while (!options.empty()) {
        const auto separator = options.find(',');
        const auto option = options.substr(0, separator);
        if (option == expected)
            return true;
        if (separator == std::string_view::npos)
            break;
        options.remove_prefix(separator + 1);
    }
    return false;
}

SourceCandidate candidate_from_mount(const backup::MountEntry& mount) {
    const fs::path root = config::normalized_path(mount.target);
    return {
        .id = {},
        .path = root.string(),
        .filesystem_uuid = mount.filesystem_uuid,
        .mount_root = root.string(),
        .local_snapshot_root = (root / ".snapshots" / "btrfs-backup").lexically_normal().string(),
    };
}

void require_btrfs_identity(const backup::MountEntry& mount) {
    if (mount.fstype != "btrfs" || mount.filesystem_uuid.empty())
        throw ValidationError("device preparation source must be on an identified Btrfs filesystem");
    if (!has_mount_option(mount.options, "rw"))
        throw ValidationError("device preparation source filesystem is not writable");
}

} // namespace

std::vector<SourceCandidate> provisioning_source_candidates(
    const std::vector<backup::MountEntry>& mounts
) {
    std::map<std::string, SourceCandidate> unique;
    for (const auto& mount : mounts) {
        if (mount.target.empty() || mount.fstype != "btrfs" || mount.filesystem_uuid.empty() ||
            !has_mount_option(mount.options, "rw"))
            continue;
        SourceCandidate candidate = candidate_from_mount(mount);
        unique.insert_or_assign(candidate.path, std::move(candidate));
    }
    std::vector<SourceCandidate> result;
    result.reserve(unique.size());
    for (auto& [path, candidate] : unique) {
        static_cast<void>(path);
        result.push_back(std::move(candidate));
    }
    return result;
}

SourceCandidate resolve_provisioning_source(
    const std::vector<backup::MountEntry>& mounts,
    const fs::path& source,
    const ProfileId& profile_id
) {
    const fs::path normalized = config::normalized_path(source);
    const auto source_mount = backup::mount_at(mounts, normalized);
    if (!source_mount.has_value())
        throw ValidationError("device preparation source is not a mounted filesystem root");
    require_btrfs_identity(*source_mount);
    SourceCandidate result = candidate_from_mount(*source_mount);
    const fs::path local = fs::path(result.local_snapshot_root) / profile_id.value();
    const auto local_mount = backup::mount_for_path(mounts, local);
    if (!local_mount.has_value())
        throw ValidationError("local snapshot directory has no mounted filesystem");
    require_btrfs_identity(*local_mount);
    if (local_mount->filesystem_uuid != source_mount->filesystem_uuid)
        throw ValidationError("local snapshot directory is not on the source Btrfs filesystem");
    result.local_snapshot_root = local.lexically_normal().string();
    return result;
}

} // namespace btrfsbackup::daemon::control
