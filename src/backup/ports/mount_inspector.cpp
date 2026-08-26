// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/ports/mount_inspector.hpp>

#include <config/model/validation.hpp>

namespace btrfsbackup {

namespace {

std::string strip_subvolume_suffix(const std::string& source) {
    const std::size_t suffix = source.find('[');
    return suffix == std::string::npos ? source : source.substr(0, suffix);
}

} // namespace

std::optional<MountEntry> mount_at(const std::vector<MountEntry>& entries, const std::filesystem::path& target) {
    const std::filesystem::path normalized_target = normalized_path(target);
    for (const MountEntry& entry : entries) {
        if (normalized_path(entry.target) == normalized_target) {
            return entry;
        }
    }
    return std::nullopt;
}

std::optional<MountEntry> mount_for_path(const std::vector<MountEntry>& entries, const std::filesystem::path& path) {
    const std::filesystem::path normalized = normalized_path(path);
    const MountEntry* best = nullptr;
    std::size_t best_size = 0;
    for (const MountEntry& entry : entries) {
        if (entry.target.empty()) {
            continue;
        }
        const std::filesystem::path target = normalized_path(entry.target);
        if (path_is_within(normalized, target)) {
            const std::size_t size = target.string().size();
            if (best == nullptr || size > best_size) {
                best = &entry;
                best_size = size;
            }
        }
    }
    return best == nullptr ? std::nullopt : std::optional<MountEntry>(*best);
}

bool paths_are_same_filesystem(
    const std::vector<MountEntry>& entries,
    const std::filesystem::path& path_a,
    const std::filesystem::path& path_b
) {
    const std::optional<MountEntry> mount_a = mount_for_path(entries, path_a);
    const std::optional<MountEntry> mount_b = mount_for_path(entries, path_b);
    if (!mount_a || !mount_b) {
        return false;
    }
    if (!mount_a->filesystem_uuid.empty() && !mount_b->filesystem_uuid.empty()) {
        return mount_a->filesystem_uuid == mount_b->filesystem_uuid;
    }
    return !mount_a->device_id.empty() && mount_a->device_id == mount_b->device_id;
}

bool mount_uses_mapper(
    const std::vector<MountEntry>& entries,
    const std::filesystem::path& mountpoint,
    const std::filesystem::path& mapper_path
) {
    const std::optional<MountEntry> mount = mount_at(entries, mountpoint);
    return mount.has_value()
        && normalized_path(strip_subvolume_suffix(mount->source)) == normalized_path(mapper_path);
}

} // namespace btrfsbackup
