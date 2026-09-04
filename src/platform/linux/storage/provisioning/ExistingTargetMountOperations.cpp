// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/provisioning/ExistingTargetMountOperations.hpp>

#include <libmount/libmount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>

#include <core/Errors.hpp>

namespace btrfsbackup::platform::linux::storage::provisioning {
namespace {

template <typename T, auto Release>
struct PointerDeleter {
    void operator()(T* value) const noexcept {
        if (value != nullptr)
            static_cast<void>(Release(value));
    }
};

using MountContext = std::unique_ptr<libmnt_context, PointerDeleter<libmnt_context, mnt_free_context>>;
using MountTable = std::unique_ptr<libmnt_table, PointerDeleter<libmnt_table, mnt_free_table>>;

void validate_path(const std::filesystem::path& path, const char* description) {
    if (!path.is_absolute() || path.lexically_normal() != path)
        throw ValidationError(std::string(description) + " path is invalid");
}

void validate_mount_target(const std::filesystem::path& target) {
    validate_path(target, "existing target mount");
    struct stat status{};
    if (::lstat(target.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) || S_ISLNK(status.st_mode) ||
        status.st_uid != 0 || (status.st_mode & (S_IWGRP | S_IWOTH)) != 0)
        throw ValidationError("existing target mount directory is not trusted");
}

MountContext new_context() {
    MountContext context(mnt_new_context());
    if (!context || mnt_context_disable_helpers(context.get(), 1) != 0)
        throw ValidationError("cannot initialize libmount without helpers");
    return context;
}

void require_success(int result, const char* operation) {
    if (result != 0)
        throw ValidationError(std::string(operation) + " failed");
}

void require_mount_success(MountContext& context, int result, const char* operation) {
    if (result == 0)
        return;
    std::array<char, 256> diagnostic{};
    static_cast<void>(mnt_context_get_excode(context.get(), result, diagnostic.data(), diagnostic.size()));
    const std::string detail = diagnostic.front() == '\0' ? std::string{} : ": " + std::string(diagnostic.data());
    throw ValidationError(std::string(operation) + " failed" + detail);
}

void verify_read_only_mount(const std::filesystem::path& target) {
    struct statvfs filesystem{};
    if (::statvfs(target.c_str(), &filesystem) != 0 || (filesystem.f_flag & ST_RDONLY) == 0)
        throw ValidationError("existing target mount is not read-only");
    MountTable table(mnt_new_table_from_file("/proc/self/mountinfo"));
    if (!table)
        throw ValidationError("cannot verify existing target mount");
    libmnt_fs* entry = mnt_table_find_target(table.get(), target.c_str(), MNT_ITER_BACKWARD);
    const char* type = entry == nullptr ? nullptr : mnt_fs_get_fstype(entry);
    const char* options = entry == nullptr ? nullptr : mnt_fs_get_options(entry);
    if (type == nullptr || std::string_view(type) != "btrfs" || options == nullptr ||
        mnt_match_options(options, "ro,nodev,nosuid,noexec") != 1)
        throw ValidationError("existing target mount options are not safe");
}

} // namespace

void LibmountExistingTargetMountOperations::mount_btrfs_read_only(
    const std::filesystem::path& source,
    const std::filesystem::path& target
) {
    validate_path(source, "existing target source");
    validate_mount_target(target);
    auto context = new_context();
    require_success(mnt_context_set_source(context.get(), source.c_str()), "setting existing target source");
    require_success(mnt_context_set_target(context.get(), target.c_str()), "setting existing target mount point");
    require_success(mnt_context_set_fstype(context.get(), "btrfs"), "setting existing target filesystem type");
    require_success(
        mnt_context_set_options(context.get(), "ro,nodev,nosuid,noexec,rescue=nologreplay"),
        "setting existing target mount options"
    );
    require_mount_success(context, mnt_context_mount(context.get()), "mounting existing target read-only");
    try {
        verify_read_only_mount(target);
    } catch (...) {
        try {
            unmount(target);
        } catch (...) {}
        throw;
    }
}

void LibmountExistingTargetMountOperations::unmount(const std::filesystem::path& target) {
    validate_mount_target(target);
    auto context = new_context();
    require_success(mnt_context_set_target(context.get(), target.c_str()), "setting existing target unmount point");
    require_mount_success(context, mnt_context_umount(context.get()), "unmounting existing target");
}

} // namespace btrfsbackup::platform::linux::storage::provisioning
