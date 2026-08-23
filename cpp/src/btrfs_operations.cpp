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

} // namespace btrfsbackup
