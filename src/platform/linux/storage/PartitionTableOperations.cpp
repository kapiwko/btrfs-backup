// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/PartitionTableOperations.hpp>

#include <fcntl.h>
#include <libfdisk/libfdisk.h>
#include <libudev.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
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

struct UdevDeleter {
    void operator()(udev* value) const noexcept {
        udev_unref(value);
    }
};

struct UdevDeviceDeleter {
    void operator()(udev_device* value) const noexcept {
        udev_device_unref(value);
    }
};

struct UdevEnumerateDeleter {
    void operator()(udev_enumerate* value) const noexcept {
        udev_enumerate_unref(value);
    }
};

struct UdevMonitorDeleter {
    void operator()(udev_monitor* value) const noexcept {
        udev_monitor_unref(value);
    }
};

using OwnedContext = std::unique_ptr<fdisk_context, ContextDeleter>;
using OwnedPartition = std::unique_ptr<fdisk_partition, PartitionDeleter>;
using OwnedUdev = std::unique_ptr<udev, UdevDeleter>;
using OwnedUdevDevice = std::unique_ptr<udev_device, UdevDeviceDeleter>;
using OwnedUdevEnumerate = std::unique_ptr<udev_enumerate, UdevEnumerateDeleter>;
using OwnedUdevMonitor = std::unique_ptr<udev_monitor, UdevMonitorDeleter>;

std::optional<std::uint64_t> parse_unsigned(const char* value) {
    if (value == nullptr || *value == '\0')
        return std::nullopt;
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value, value + std::char_traits<char>::length(value), result);
    if (parsed.ec != std::errc{} || *parsed.ptr != '\0')
        return std::nullopt;
    return result;
}

class PartitionAppearanceMonitor final {
  public:
    explicit PartitionAppearanceMonitor(dev_t parent) : parent_(parent), udev_(udev_new()) {
        if (!udev_)
            throw ValidationError("cannot initialize udev partition monitor");
        monitor_.reset(udev_monitor_new_from_netlink(udev_.get(), "udev"));
        if (!monitor_ ||
            udev_monitor_filter_add_match_subsystem_devtype(monitor_.get(), "block", "partition") < 0 ||
            udev_monitor_enable_receiving(monitor_.get()) < 0)
            throw ValidationError("cannot enable udev partition monitor");
        if (udev_monitor_get_fd(monitor_.get()) < 0)
            throw ValidationError("cannot access udev partition monitor");
    }

    void wait_for(
        std::uint64_t partition_number,
        std::uint64_t start_512_sectors,
        std::uint64_t size_512_sectors,
        std::chrono::milliseconds timeout
    ) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            if (exact_partition_exists(partition_number, start_512_sectors, size_512_sectors))
                return;
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
                throw ValidationError("created partition did not appear before timeout");
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            pollfd descriptor{
                .fd = udev_monitor_get_fd(monitor_.get()),
                .events = POLLIN,
                .revents = 0,
            };
            const auto bounded = std::min<std::int64_t>(remaining.count() + 1, std::numeric_limits<int>::max());
            const int result = ::poll(&descriptor, 1, static_cast<int>(bounded));
            if (result < 0) {
                if (errno == EINTR)
                    continue;
                throw ValidationError("waiting for udev partition event failed");
            }
            if (result == 0)
                continue;
            if ((descriptor.revents & POLLIN) != 0) {
                OwnedUdevDevice event(udev_monitor_receive_device(monitor_.get()));
                continue;
            }
            throw ValidationError("udev partition monitor failed");
        }
    }

  private:
    bool exact_partition_exists(
        std::uint64_t partition_number,
        std::uint64_t start_512_sectors,
        std::uint64_t size_512_sectors
    ) const {
        OwnedUdevEnumerate enumeration(udev_enumerate_new(udev_.get()));
        if (!enumeration || udev_enumerate_add_match_subsystem(enumeration.get(), "block") < 0 ||
            udev_enumerate_scan_devices(enumeration.get()) < 0)
            throw ValidationError("cannot enumerate created partition");
        udev_list_entry* devices = udev_enumerate_get_list_entry(enumeration.get());
        udev_list_entry* entry = nullptr;
        udev_list_entry_foreach(entry, devices) {
            const char* syspath = udev_list_entry_get_name(entry);
            OwnedUdevDevice candidate(udev_device_new_from_syspath(udev_.get(), syspath));
            if (!candidate)
                continue;
            const char* device_type = udev_device_get_devtype(candidate.get());
            if (device_type == nullptr || std::string_view(device_type) != "partition")
                continue;
            udev_device* parent = udev_device_get_parent_with_subsystem_devtype(candidate.get(), "block", "disk");
            if (parent == nullptr || udev_device_get_devnum(parent) != parent_)
                continue;
            const auto number = parse_unsigned(udev_device_get_sysattr_value(candidate.get(), "partition"));
            const auto start = parse_unsigned(udev_device_get_sysattr_value(candidate.get(), "start"));
            const auto size = parse_unsigned(udev_device_get_sysattr_value(candidate.get(), "size"));
            if (number == partition_number && start == start_512_sectors && size == size_512_sectors &&
                udev_device_get_devnode(candidate.get()) != nullptr)
                return true;
        }
        return false;
    }

    dev_t parent_;
    OwnedUdev udev_;
    OwnedUdevMonitor monitor_;
};

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
        if (!fdisk_partition_has_start(partition.get()) || !fdisk_partition_has_size(partition.get()))
            throw ValidationError("libfdisk did not resolve partition geometry");
        const std::uint64_t logical_sector_size = fdisk_get_sector_size(context.get());
        if (logical_sector_size == 0 || logical_sector_size % 512 != 0)
            throw ValidationError("partition table sector size is unsupported");
        const std::uint64_t sysfs_sector_ratio = logical_sector_size / 512;
        const std::uint64_t partition_start = fdisk_partition_get_start(partition.get());
        const std::uint64_t partition_size = fdisk_partition_get_size(partition.get());
        if (partition_start > std::numeric_limits<std::uint64_t>::max() / sysfs_sector_ratio ||
            partition_size > std::numeric_limits<std::uint64_t>::max() / sysfs_sector_ratio)
            throw ValidationError("partition geometry exceeds supported range");
        PartitionAppearanceMonitor appearance(status.st_rdev);
        require_success(fdisk_wipe_partition(context.get(), partition_number, 1), "enabling partition wipe");
        require_success(fdisk_write_disklabel(context.get()), "writing GPT partition table");
        if (::fsync(descriptor.get()) != 0)
            throw ValidationError("syncing GPT partition table failed");
        require_success(fdisk_reread_partition_table(context.get()), "rereading GPT partition table");
        appearance.wait_for(
            partition_number + 1,
            partition_start * sysfs_sector_ratio,
            partition_size * sysfs_sector_ratio,
            std::chrono::seconds(10)
        );
        require_success(fdisk_deassign_device(context.get(), 0), "releasing partition table device");
    } catch (...) {
        if (fdisk_get_devfd(context.get()) >= 0)
            static_cast<void>(fdisk_deassign_device(context.get(), 1));
        throw;
    }
}

} // namespace btrfsbackup::platform::linux::storage
