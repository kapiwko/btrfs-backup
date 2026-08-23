#include <btrfsbackup/mount_info.hpp>

#include <libmount/libmount.h>

#include <memory>
#include <set>

#include <btrfsbackup/errors.hpp>

namespace btrfsbackup {

namespace {

std::string c_string(const char* value) {
    return value == nullptr ? "" : value;
}

} // namespace

std::vector<MountEntry> read_mount_table(const std::filesystem::path& mountinfo_path) {
    std::unique_ptr<libmnt_table, decltype(&mnt_unref_table)> table(mnt_new_table(), mnt_unref_table);
    if (!table || mnt_table_parse_file(table.get(), mountinfo_path.c_str()) != 0) {
        throw ValidationError("could not read mount table");
    }
    std::unique_ptr<libmnt_iter, decltype(&mnt_free_iter)> iter(mnt_new_iter(MNT_ITER_FORWARD), mnt_free_iter);
    if (!iter) {
        throw ValidationError("could not iterate mount table");
    }

    std::vector<MountEntry> entries;
    libmnt_fs* mount = nullptr;
    while (mnt_table_next_fs(table.get(), iter.get(), &mount) == 0) {
        entries.push_back({
            .source = c_string(mnt_fs_get_source(mount)),
            .target = c_string(mnt_fs_get_target(mount)),
            .fstype = c_string(mnt_fs_get_fstype(mount)),
        });
    }
    return entries;
}

std::vector<std::string> btrfs_mount_targets(const std::filesystem::path& mountinfo_path) {
    std::set<std::string> unique;
    for (const MountEntry& entry : read_mount_table(mountinfo_path)) {
        if (entry.fstype == "btrfs" && !entry.target.empty()) {
            unique.insert(entry.target);
        }
    }
    return {unique.begin(), unique.end()};
}

} // namespace btrfsbackup
