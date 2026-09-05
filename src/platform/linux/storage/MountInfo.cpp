// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/MountInfo.hpp>

#include <blkid/blkid.h>
#include <libmount/libmount.h>

#include <cstdlib>
#include <array>
#include <memory>
#include <set>
#include <sstream>
#include <sys/sysmacros.h>

#include <platform/linux/storage/DeviceInfo.hpp>
#include <core/Errors.hpp>

namespace btrfsbackup::platform::linux::storage {

namespace {

std::string c_string(const char* value) {
    return value == nullptr ? "" : value;
}

std::string device_id(dev_t device) {
    if (device == 0) {
        return "";
    }
    std::ostringstream output;
    output << major(device) << ":" << minor(device);
    return output.str();
}

} // namespace

std::vector<btrfsbackup::backup::MountEntry> read_mount_table(const std::filesystem::path& mountinfo_path) {
    return read_mount_table(mountinfo_path, blkid_filesystem_uuid);
}

std::string blkid_filesystem_uuid(const std::string& source) {
    if (source.empty() || source.front() != '/') {
        return "";
    }
    char* value = blkid_get_tag_value(nullptr, "UUID", source.c_str());
    if (value == nullptr) {
        return "";
    }
    std::string uuid = value;
    std::free(value);
    return uuid;
}

std::vector<btrfsbackup::backup::MountEntry> read_mount_table(const std::filesystem::path& mountinfo_path, const FilesystemUuidResolver& filesystem_uuid_resolver) {
    std::unique_ptr<libmnt_table, decltype(&mnt_unref_table)> table(mnt_new_table(), mnt_unref_table);
    if (!table || mnt_table_parse_file(table.get(), mountinfo_path.c_str()) != 0) {
        throw ValidationError("could not read mount table");
    }
    std::unique_ptr<libmnt_iter, decltype(&mnt_free_iter)> iter(mnt_new_iter(MNT_ITER_FORWARD), mnt_free_iter);
    if (!iter) {
        throw ValidationError("could not iterate mount table");
    }

    std::vector<btrfsbackup::backup::MountEntry> entries;
    libmnt_fs* mount = nullptr;
    while (mnt_table_next_fs(table.get(), iter.get(), &mount) == 0) {
        std::string source = c_string(mnt_fs_get_source(mount));
        std::string fstype = c_string(mnt_fs_get_fstype(mount));
        entries.push_back({
            .source = source,
            .target = c_string(mnt_fs_get_target(mount)),
            .fstype = fstype,
            .root = c_string(mnt_fs_get_root(mount)),
            .options = c_string(mnt_fs_get_options(mount)),
            .mount_id = mnt_fs_get_id(mount),
            .device_id = device_id(mnt_fs_get_devno(mount)),
            .filesystem_uuid = fstype == "btrfs" ? filesystem_uuid_resolver(strip_subvolume_suffix(source)) : "",
        });
    }
    return entries;
}

std::vector<std::string> btrfs_mount_targets(const std::filesystem::path& mountinfo_path) {
    std::set<std::string> unique;
    for (const btrfsbackup::backup::MountEntry& entry : read_mount_table(mountinfo_path)) {
        if (entry.fstype == "btrfs" && !entry.target.empty()) {
            unique.insert(entry.target);
        }
    }
    return {unique.begin(), unique.end()};
}

void unmount_filesystem(const std::filesystem::path& target) {
    std::unique_ptr<libmnt_context, decltype(&mnt_free_context)> context(
        mnt_new_context(),
        mnt_free_context
    );
    if (!context || mnt_context_set_target(context.get(), target.c_str()) != 0)
        throw ValidationError("could not prepare unmount for " + target.string());

    const int result = mnt_context_umount(context.get());
    if (result == 0 && mnt_context_get_status(context.get()) == 1)
        return;

    std::array<char, 256> message{};
    (void)mnt_context_get_excode(context.get(), result, message.data(), message.size());
    throw ValidationError(
        "could not unmount " + target.string() +
        (message.front() == '\0' ? std::string{} : ": " + std::string(message.data()))
    );
}

LinuxMountInspector::LinuxMountInspector(
    std::filesystem::path mountinfo,
    FilesystemUuidResolver uuid_resolver
) : mountinfo_(std::move(mountinfo)),
    uuid_resolver_(std::move(uuid_resolver)) {
}

std::vector<btrfsbackup::backup::MountEntry> LinuxMountInspector::inspect() const {
    return read_mount_table(mountinfo_, uuid_resolver_);
}

} // namespace btrfsbackup::platform::linux::storage
