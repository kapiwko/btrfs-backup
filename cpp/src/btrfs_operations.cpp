#include <btrfsbackup/btrfs_operations.hpp>

#include <filesystem>
#include <optional>

#include <btrfsutil.h>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/snapshot_inventory.hpp>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void throw_btrfs_error(const std::string& operation, const fs::path& path, enum btrfs_util_error error) {
    throw btrfsbackup::ValidationError(operation + " failed for " + path.string() + ": " + btrfs_util_strerror(error));
}

} // namespace

namespace btrfsbackup {

bool IBtrfsOperations::is_subvolume_beneath(const SafeDirectoryRoot&, const fs::path& path) {
    return is_subvolume(path);
}

std::optional<SnapshotMetadata> IBtrfsOperations::read_snapshot_metadata_beneath(
    const SafeDirectoryRoot&,
    const fs::path& path
) {
    return read_snapshot_metadata(path);
}

void IBtrfsOperations::create_readonly_snapshot_beneath(
    const SafeDirectoryRoot&,
    const fs::path& source,
    const SafeDirectoryRoot&,
    const fs::path& target
) {
    create_readonly_snapshot(source, target);
}

void IBtrfsOperations::delete_subvolume_beneath(const SafeDirectoryRoot&, const fs::path& path) {
    delete_subvolume(path);
}

bool LibBtrfsOperations::is_subvolume(const fs::path& path) {
    enum btrfs_util_error error = btrfs_util_subvolume_is_valid(path.c_str());
    if (error == BTRFS_UTIL_OK) {
        return true;
    }
    if (error == BTRFS_UTIL_ERROR_NOT_BTRFS
        || error == BTRFS_UTIL_ERROR_NOT_SUBVOLUME
        || error == BTRFS_UTIL_ERROR_SUBVOLUME_NOT_FOUND) {
        return false;
    }
    throw_btrfs_error("Btrfs subvolume check", path, error);
}

std::optional<SnapshotMetadata> LibBtrfsOperations::read_snapshot_metadata(const fs::path& path) {
    return read_btrfs_snapshot_metadata(path);
}

void LibBtrfsOperations::create_readonly_snapshot(const fs::path& source, const fs::path& target) {
    enum btrfs_util_error error = btrfs_util_subvolume_snapshot(
        source.c_str(),
        target.c_str(),
        BTRFS_UTIL_CREATE_SNAPSHOT_READ_ONLY,
        nullptr,
        nullptr
    );
    if (error != BTRFS_UTIL_OK) {
        throw_btrfs_error("Btrfs readonly snapshot creation", target, error);
    }
}

void LibBtrfsOperations::delete_subvolume(const fs::path& path) {
    enum btrfs_util_error error = btrfs_util_subvolume_delete(path.c_str(), 0);
    if (error != BTRFS_UTIL_OK) {
        throw_btrfs_error("Btrfs subvolume deletion", path, error);
    }
}

bool LibBtrfsOperations::is_subvolume_beneath(const SafeDirectoryRoot& root, const fs::path& path) {
    SafeDirectoryHandle handle = root.open_directory(path);
    return is_subvolume(handle.proc_path());
}

std::optional<SnapshotMetadata> LibBtrfsOperations::read_snapshot_metadata_beneath(
    const SafeDirectoryRoot& root,
    const fs::path& path
) {
    SafeDirectoryHandle handle = root.open_directory(path);
    return read_snapshot_metadata(handle.proc_path());
}

void LibBtrfsOperations::create_readonly_snapshot_beneath(
    const SafeDirectoryRoot& source_root,
    const fs::path& source,
    const SafeDirectoryRoot& target_root,
    const fs::path& target
) {
    SafeDirectoryHandle source_handle = source_root.open_directory(source);
    target_root.ensure_directory(target.parent_path());
    if (target_root.exists(target)) {
        throw ValidationError("snapshot target already exists: " + target.string());
    }
    SafeDirectoryHandle target_parent = target_root.open_directory(target.parent_path());
    create_readonly_snapshot(source_handle.proc_path(), target_parent.proc_path() / target.filename());
}

void LibBtrfsOperations::delete_subvolume_beneath(const SafeDirectoryRoot& root, const fs::path& path) {
    root.delete_subvolume(path);
}

} // namespace btrfsbackup
