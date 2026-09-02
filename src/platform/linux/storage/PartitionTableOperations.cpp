// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/PartitionTableOperations.hpp>

#include <fcntl.h>
#include <libfdisk/libfdisk.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <charconv>
#include <memory>
#include <system_error>

#include <core/Errors.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>

namespace btrfsbackup::platform::linux::storage {
namespace {

struct ContextDeleter {
    void operator()(fdisk_context* context) const noexcept {
        fdisk_unref_context(context);
    }
};

struct PartitionDeleter {
    void operator()(fdisk_partition* partition) const noexcept {
        fdisk_unref_partition(partition);
    }
};

using OwnedContext = std::unique_ptr<fdisk_context, ContextDeleter>;
using OwnedPartition = std::unique_ptr<fdisk_partition, PartitionDeleter>;

std::pair<unsigned, unsigned> parse_major_minor(const std::string& value) {
    const auto separator = value.find(':');
    if (separator == std::string::npos)
        throw ValidationError("expected block device identity is invalid");
    unsigned major_number = 0;
    unsigned minor_number = 0;
    const auto major_result = std::from_chars(value.data(), value.data() + separator, major_number);
    const auto minor_result = std::from_chars(value.data() + separator + 1, value.data() + value.size(), minor_number);
    if (major_result.ec != std::errc{} || major_result.ptr != value.data() + separator ||
        minor_result.ec != std::errc{} || minor_result.ptr != value.data() + value.size())
        throw ValidationError("expected block device identity is invalid");
    return {major_number, minor_number};
}

void require_success(int result, const char* operation) {
    if (result != 0)
        throw ValidationError(std::string(operation) + " failed");
}

} // namespace

void LibfdiskPartitionTableOperations::replace_with_single_gpt_partition(
    const std::filesystem::path& device,
    const std::string& expected_major_minor
) {
    if (!device.is_absolute() || device.lexically_normal() != device)
        throw ValidationError("partition table device path is invalid");
    const auto [expected_major, expected_minor] = parse_major_minor(expected_major_minor);
    OwnedFileDescriptor descriptor(::open(device.c_str(), O_RDWR | O_CLOEXEC | O_EXCL | O_NOFOLLOW));
    if (!descriptor.valid())
        throw ValidationError("cannot exclusively open partition table device");
    struct stat status{};
    if (::fstat(descriptor.get(), &status) != 0 || !S_ISBLK(status.st_mode))
        throw ValidationError("partition table target is not a block device");
    if (major(status.st_rdev) != expected_major || minor(status.st_rdev) != expected_minor)
        throw ValidationError("partition table device identity changed");
    if (::flock(descriptor.get(), LOCK_EX | LOCK_NB) != 0)
        throw ValidationError("partition table device is locked by another process");

    OwnedContext context(fdisk_new_context());
    if (!context)
        throw ValidationError("cannot allocate libfdisk context");
    require_success(
        fdisk_assign_device_by_fd(context.get(), descriptor.get(), device.c_str(), 0),
        "assigning partition table device"
    );
    try {
        require_success(fdisk_disable_dialogs(context.get(), 1), "disabling libfdisk dialogs");
        require_success(fdisk_enable_wipe(context.get(), 1), "enabling partition signature wiping");
        require_success(fdisk_create_disklabel(context.get(), "gpt"), "creating GPT partition table");

        OwnedPartition partition(fdisk_new_partition());
        if (!partition)
            throw ValidationError("cannot allocate libfdisk partition");
        require_success(fdisk_partition_start_follow_default(partition.get(), 1), "selecting partition start");
        require_success(fdisk_partition_end_follow_default(partition.get(), 1), "selecting partition end");
        require_success(fdisk_partition_partno_follow_default(partition.get(), 1), "selecting partition number");
        std::size_t partition_number = 0;
        require_success(
            fdisk_add_partition(context.get(), partition.get(), &partition_number),
            "adding GPT partition"
        );
        require_success(fdisk_wipe_partition(context.get(), partition_number, 1), "enabling partition wipe");
        require_success(fdisk_write_disklabel(context.get()), "writing GPT partition table");
        if (::fsync(descriptor.get()) != 0)
            throw ValidationError("syncing GPT partition table failed");
        require_success(fdisk_reread_partition_table(context.get()), "rereading GPT partition table");
        require_success(fdisk_deassign_device(context.get(), 0), "releasing partition table device");
    } catch (...) {
        if (fdisk_get_devfd(context.get()) >= 0)
            static_cast<void>(fdisk_deassign_device(context.get(), 1));
        throw;
    }
}

} // namespace btrfsbackup::platform::linux::storage
