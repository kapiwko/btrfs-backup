// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/FilesystemSpaceProbe.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <limits>
#include <string>
#include <system_error>

#include <core/Errors.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>

namespace btrfsbackup::platform::linux::storage {

namespace {

std::uint64_t checked_bytes(unsigned long blocks, unsigned long block_size) {
    if (block_size != 0 && blocks > std::numeric_limits<std::uint64_t>::max() / block_size) {
        throw ValidationError("filesystem space statistics overflow");
    }
    return static_cast<std::uint64_t>(blocks) * static_cast<std::uint64_t>(block_size);
}

} // namespace

btrfsbackup::backup::FilesystemSpace FilesystemSpaceProbe::measure_verified_mount(
    const std::filesystem::path& mount_point,
    const btrfsbackup::backup::MountEntry& expected_mount
) const {
    int raw_descriptor;
    do {
        raw_descriptor = open(mount_point.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    } while (raw_descriptor < 0 && errno == EINTR);
    if (raw_descriptor < 0) {
        throw ValidationError("could not open verified target mount: " + std::error_code(errno, std::generic_category()).message());
    }
    const btrfsbackup::platform::linux::OwnedFileDescriptor descriptor(raw_descriptor);

    if (expected_mount.mount_id <= 0) {
        throw ValidationError("verified target mount has no mount identity");
    }
    struct statx identity{};
    if (statx(descriptor.get(), "", AT_EMPTY_PATH | AT_STATX_SYNC_AS_STAT, STATX_MNT_ID, &identity) != 0) {
        throw ValidationError("could not verify target mount identity");
    }
    if ((identity.stx_mask & STATX_MNT_ID) == 0 || identity.stx_mnt_id != static_cast<std::uint64_t>(expected_mount.mount_id)) {
        throw ValidationError("target mount identity changed before space measurement");
    }

    struct statvfs statistics{};
    if (fstatvfs(descriptor.get(), &statistics) != 0) {
        throw ValidationError("could not read target filesystem space statistics");
    }
    const unsigned long block_size = statistics.f_frsize == 0 ? statistics.f_bsize : statistics.f_frsize;
    btrfsbackup::backup::FilesystemSpace result{
        .capacity_bytes = checked_bytes(statistics.f_blocks, block_size),
        .free_bytes = checked_bytes(statistics.f_bfree, block_size),
        .available_bytes = checked_bytes(statistics.f_bavail, block_size),
    };
    if (!result.valid()) {
        throw ValidationError("target filesystem returned invalid space statistics");
    }
    return result;
}

} // namespace btrfsbackup::platform::linux::storage
