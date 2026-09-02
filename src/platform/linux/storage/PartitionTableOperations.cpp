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
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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

struct TableDeleter {
    void operator()(fdisk_table* table) const noexcept {
        fdisk_unref_table(table);
    }
};

struct ScriptDeleter {
    void operator()(fdisk_script* script) const noexcept {
        fdisk_unref_script(script);
    }
};

struct FileDeleter {
    void operator()(FILE* file) const noexcept {
        static_cast<void>(std::fclose(file));
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
using OwnedTable = std::unique_ptr<fdisk_table, TableDeleter>;
using OwnedScript = std::unique_ptr<fdisk_script, ScriptDeleter>;
using OwnedFile = std::unique_ptr<FILE, FileDeleter>;
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

std::optional<std::filesystem::path> find_exact_partition(
    udev* udev_context,
    dev_t parent_device,
    std::uint64_t partition_number,
    std::uint64_t start_512_sectors,
    std::uint64_t size_512_sectors
) {
    OwnedUdevEnumerate enumeration(udev_enumerate_new(udev_context));
    if (!enumeration || udev_enumerate_add_match_subsystem(enumeration.get(), "block") < 0 ||
        udev_enumerate_scan_devices(enumeration.get()) < 0)
        throw ValidationError("cannot enumerate created partition");
    udev_list_entry* devices = udev_enumerate_get_list_entry(enumeration.get());
    udev_list_entry* entry = nullptr;
    udev_list_entry_foreach(entry, devices) {
        const char* syspath = udev_list_entry_get_name(entry);
        OwnedUdevDevice candidate(udev_device_new_from_syspath(udev_context, syspath));
        if (!candidate)
            continue;
        const char* device_type = udev_device_get_devtype(candidate.get());
        if (device_type == nullptr || std::string_view(device_type) != "partition")
            continue;
        udev_device* parent = udev_device_get_parent_with_subsystem_devtype(candidate.get(), "block", "disk");
        if (parent == nullptr || udev_device_get_devnum(parent) != parent_device)
            continue;
        const auto number = parse_unsigned(udev_device_get_sysattr_value(candidate.get(), "partition"));
        const auto start = parse_unsigned(udev_device_get_sysattr_value(candidate.get(), "start"));
        const auto size = parse_unsigned(udev_device_get_sysattr_value(candidate.get(), "size"));
        if (number == partition_number && start == start_512_sectors && size == size_512_sectors &&
            udev_device_get_devnode(candidate.get()) != nullptr)
            return std::filesystem::path(udev_device_get_devnode(candidate.get()));
    }
    return std::nullopt;
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

    std::filesystem::path wait_for(
        std::uint64_t partition_number,
        std::uint64_t start_512_sectors,
        std::uint64_t size_512_sectors,
        std::chrono::milliseconds timeout
    ) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            if (const auto path = exact_partition(partition_number, start_512_sectors, size_512_sectors);
                path.has_value())
                return *path;
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
    std::optional<std::filesystem::path> exact_partition(
        std::uint64_t partition_number,
        std::uint64_t start_512_sectors,
        std::uint64_t size_512_sectors
    ) const {
        return find_exact_partition(
            udev_.get(),
            parent_,
            partition_number,
            start_512_sectors,
            size_512_sectors
        );
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

std::string lowercase(std::string value) {
    std::ranges::transform(value, value.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return value;
}

void validate_device_path(const std::filesystem::path& device) {
    if (!device.is_absolute() || device.lexically_normal() != device)
        throw ValidationError("partition table device path is invalid");
}

void validate_partition_table_identity(
    fdisk_context* context,
    PartitionTableFormat expected_format,
    const std::string& expected_partition_table_id,
    std::uint32_t expected_logical_sector_size
) {
    if (fdisk_get_sector_size(context) != expected_logical_sector_size)
        throw ValidationError("partition table sector size changed");
    if (expected_format == PartitionTableFormat::None) {
        if (fdisk_has_label(context) || !expected_partition_table_id.empty())
            throw ValidationError("partition table identity changed");
        return;
    }
    const bool expected_label = expected_format == PartitionTableFormat::Gpt
        ? fdisk_is_label(context, GPT)
        : fdisk_is_label(context, DOS);
    if (!expected_label)
        throw ValidationError("partition table type changed");
    if (expected_partition_table_id.empty())
        throw ValidationError("partition table identifier is unavailable");
    char* raw_identifier = nullptr;
    if (fdisk_get_disklabel_id(context, &raw_identifier) != 0 || raw_identifier == nullptr)
        throw ValidationError("cannot read partition table identifier");
    const std::unique_ptr<char, decltype(&std::free)> identifier(raw_identifier, &std::free);
    if (lowercase(identifier.get()) != lowercase(expected_partition_table_id))
        throw ValidationError("partition table identity changed");
}

void validate_gpt_identity(
    fdisk_context* context,
    const std::string& expected_partition_table_id,
    std::uint32_t expected_logical_sector_size
) {
    validate_partition_table_identity(
        context,
        PartitionTableFormat::Gpt,
        expected_partition_table_id,
        expected_logical_sector_size
    );
}

bool has_exact_free_region(
    fdisk_context* context,
    std::uint64_t free_start_sector,
    std::uint64_t free_sector_count
) {
    fdisk_table* raw_free_spaces = nullptr;
    require_success(fdisk_get_freespaces(context, &raw_free_spaces), "reading unallocated space");
    OwnedTable free_spaces(raw_free_spaces);
    for (std::size_t index = 0; index < fdisk_table_get_nents(free_spaces.get()); ++index) {
        fdisk_partition* region = fdisk_table_get_partition(free_spaces.get(), index);
        if (region != nullptr && fdisk_partition_has_start(region) && fdisk_partition_has_size(region) &&
            fdisk_partition_get_start(region) == free_start_sector &&
            fdisk_partition_get_size(region) == free_sector_count)
            return true;
    }
    return false;
}

void validate_gpt_free_region(
    fdisk_context* context,
    const std::string& expected_partition_table_id,
    std::uint32_t expected_logical_sector_size,
    std::uint64_t free_start_sector,
    std::uint64_t free_sector_count
) {
    validate_gpt_identity(context, expected_partition_table_id, expected_logical_sector_size);
    if (!has_exact_free_region(context, free_start_sector, free_sector_count))
        throw ValidationError("selected unallocated region changed");
}

void validate_free_space_request(
    const std::string& expected_partition_table_id,
    std::uint32_t expected_logical_sector_size,
    std::uint64_t free_start_sector,
    std::uint64_t free_sector_count
) {
    if (expected_partition_table_id.empty() || expected_logical_sector_size == 0 || free_sector_count == 0 ||
        free_start_sector > std::numeric_limits<std::uint64_t>::max() - (free_sector_count - 1))
        throw ValidationError("free-space partition request is incomplete");
}

bool regions_overlap(
    std::uint64_t left_start,
    std::uint64_t left_size,
    std::uint64_t right_start,
    std::uint64_t right_size
) {
    if (left_size == 0 || right_size == 0)
        return false;
    return left_start <= right_start
        ? right_start - left_start < left_size
        : left_start - right_start < right_size;
}

} // namespace

std::string LibfdiskPartitionTableOperations::snapshot_partition_table(
    const std::filesystem::path& device,
    const std::string& expected_major_minor,
    PartitionTableFormat expected_format,
    const std::string& expected_partition_table_id,
    std::uint32_t expected_logical_sector_size
) const {
    constexpr std::size_t maximum_snapshot_size = 1024 * 1024;
    validate_device_path(device);
    if (expected_logical_sector_size == 0 ||
        (expected_format != PartitionTableFormat::None && expected_partition_table_id.empty()))
        throw ValidationError("partition table snapshot request is incomplete");
    const auto [expected_major, expected_minor] = parse_major_minor(expected_major_minor);
    OwnedFileDescriptor descriptor(::open(device.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!descriptor.valid())
        throw ValidationError("cannot open partition table device for snapshot");
    struct stat status{};
    if (::fstat(descriptor.get(), &status) != 0 || !S_ISBLK(status.st_mode))
        throw ValidationError("partition table target is not a block device");
    if (major(status.st_rdev) != expected_major || minor(status.st_rdev) != expected_minor)
        throw ValidationError("partition table device identity changed");
    if (::flock(descriptor.get(), LOCK_SH | LOCK_NB) != 0)
        throw ValidationError("partition table device is locked by another process");
    OwnedContext context(fdisk_new_context());
    if (!context)
        throw ValidationError("cannot allocate libfdisk context");
    require_success(
        fdisk_assign_device_by_fd(context.get(), descriptor.get(), device.c_str(), 1),
        "assigning partition table device"
    );
    try {
        require_success(fdisk_disable_dialogs(context.get(), 1), "disabling libfdisk dialogs");
        validate_partition_table_identity(
            context.get(),
            expected_format,
            expected_partition_table_id,
            expected_logical_sector_size
        );
        if (expected_format == PartitionTableFormat::None) {
            const std::string result =
                "label: none\nsector-size: " + std::to_string(expected_logical_sector_size) + "\n";
            require_success(fdisk_deassign_device(context.get(), 1), "releasing partition table device");
            return result;
        }
        OwnedScript script(fdisk_new_script(context.get()));
        if (!script)
            throw ValidationError("cannot allocate partition table snapshot");
        require_success(fdisk_script_read_context(script.get(), context.get()), "reading partition table snapshot");
        OwnedFile file(std::tmpfile());
        if (!file)
            throw ValidationError("cannot allocate partition table snapshot file");
        require_success(fdisk_script_write_file(script.get(), file.get()), "serializing partition table snapshot");
        if (std::fflush(file.get()) != 0)
            throw ValidationError("flushing partition table snapshot failed");
        const long snapshot_size = std::ftell(file.get());
        if (snapshot_size <= 0 || snapshot_size > static_cast<long>(maximum_snapshot_size))
            throw ValidationError("partition table snapshot size is invalid");
        if (std::fseek(file.get(), 0, SEEK_SET) != 0)
            throw ValidationError("rewinding partition table snapshot failed");
        std::string result(static_cast<std::size_t>(snapshot_size), '\0');
        if (std::fread(result.data(), 1, result.size(), file.get()) != result.size())
            throw ValidationError("reading partition table snapshot failed");
        require_success(fdisk_deassign_device(context.get(), 1), "releasing partition table device");
        return result;
    } catch (...) {
        if (fdisk_get_devfd(context.get()) >= 0)
            static_cast<void>(fdisk_deassign_device(context.get(), 1));
        throw;
    }
}

PlannedPartitionGeometry LibfdiskPartitionTableOperations::plan_partition_in_free_space(
    const std::filesystem::path& device,
    const std::string& expected_major_minor,
    const std::string& expected_partition_table_id,
    std::uint32_t expected_logical_sector_size,
    std::uint64_t free_start_sector,
    std::uint64_t free_sector_count
) const {
    validate_device_path(device);
    validate_free_space_request(
        expected_partition_table_id,
        expected_logical_sector_size,
        free_start_sector,
        free_sector_count
    );
    const auto [expected_major, expected_minor] = parse_major_minor(expected_major_minor);
    OwnedFileDescriptor descriptor(::open(device.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!descriptor.valid())
        throw ValidationError("cannot open partition table device for planning");
    struct stat status{};
    if (::fstat(descriptor.get(), &status) != 0 || !S_ISBLK(status.st_mode))
        throw ValidationError("partition table target is not a block device");
    if (major(status.st_rdev) != expected_major || minor(status.st_rdev) != expected_minor)
        throw ValidationError("partition table device identity changed");
    if (::flock(descriptor.get(), LOCK_SH | LOCK_NB) != 0)
        throw ValidationError("partition table device is locked by another process");

    OwnedContext context(fdisk_new_context());
    if (!context)
        throw ValidationError("cannot allocate libfdisk context");
    require_success(
        fdisk_assign_device_by_fd(context.get(), descriptor.get(), device.c_str(), 1),
        "assigning partition table device"
    );
    try {
        require_success(fdisk_disable_dialogs(context.get(), 1), "disabling libfdisk dialogs");
        validate_gpt_free_region(
            context.get(),
            expected_partition_table_id,
            expected_logical_sector_size,
            free_start_sector,
            free_sector_count
        );

        const std::uint64_t free_end = free_start_sector + free_sector_count - 1;
        const std::uint64_t aligned_start = fdisk_align_lba(context.get(), free_start_sector, FDISK_ALIGN_UP);
        if (aligned_start < free_start_sector || aligned_start > free_end ||
            fdisk_lba_is_phy_aligned(context.get(), aligned_start) != 1)
            throw ValidationError("unallocated region is too small after alignment");

        OwnedPartition partition(fdisk_new_partition());
        if (!partition)
            throw ValidationError("cannot allocate libfdisk partition");
        require_success(fdisk_partition_set_start(partition.get(), aligned_start), "selecting partition start");
        require_success(fdisk_partition_end_follow_default(partition.get(), 1), "selecting partition end");
        require_success(fdisk_partition_partno_follow_default(partition.get(), 1), "selecting partition number");
        std::size_t partition_number = 0;
        require_success(fdisk_add_partition(context.get(), partition.get(), &partition_number), "planning GPT partition");
        if (!fdisk_partition_has_start(partition.get()) || !fdisk_partition_has_size(partition.get()) ||
            partition_number >= std::numeric_limits<std::uint32_t>::max())
            throw ValidationError("libfdisk did not resolve partition geometry");
        const std::uint64_t partition_start = fdisk_partition_get_start(partition.get());
        const std::uint64_t partition_size = fdisk_partition_get_size(partition.get());
        if (partition_start < free_start_sector || partition_start > free_end || partition_size == 0 ||
            partition_size > free_end - partition_start + 1)
            throw ValidationError("libfdisk planned a partition outside the selected free region");
        const PlannedPartitionGeometry result{
            .start_sector = partition_start,
            .sector_count = partition_size,
            .partition_number = static_cast<std::uint32_t>(partition_number + 1),
        };
        require_success(fdisk_deassign_device(context.get(), 1), "releasing partition table device");
        return result;
    } catch (...) {
        if (fdisk_get_devfd(context.get()) >= 0)
            static_cast<void>(fdisk_deassign_device(context.get(), 1));
        throw;
    }
}

PartitionCreationInspection LibfdiskPartitionTableOperations::inspect_partition_creation(
    const std::filesystem::path& device,
    const std::string& expected_major_minor,
    const std::string& expected_partition_table_id,
    std::uint32_t expected_logical_sector_size,
    std::uint64_t original_free_start_sector,
    std::uint64_t original_free_sector_count,
    const PlannedPartitionGeometry& geometry
) const {
    validate_device_path(device);
    validate_free_space_request(
        expected_partition_table_id,
        expected_logical_sector_size,
        original_free_start_sector,
        original_free_sector_count
    );
    const std::uint64_t free_end = original_free_start_sector + original_free_sector_count - 1;
    if (geometry.partition_number == 0 || geometry.sector_count == 0 ||
        geometry.start_sector < original_free_start_sector || geometry.start_sector > free_end ||
        geometry.sector_count > free_end - geometry.start_sector + 1)
        throw ValidationError("planned partition geometry exceeds the selected free region");
    const auto [expected_major, expected_minor] = parse_major_minor(expected_major_minor);
    OwnedFileDescriptor descriptor(::open(device.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!descriptor.valid())
        throw ValidationError("cannot open partition table device for inspection");
    struct stat status{};
    if (::fstat(descriptor.get(), &status) != 0 || !S_ISBLK(status.st_mode))
        throw ValidationError("partition table target is not a block device");
    if (major(status.st_rdev) != expected_major || minor(status.st_rdev) != expected_minor)
        throw ValidationError("partition table device identity changed");
    if (::flock(descriptor.get(), LOCK_SH | LOCK_NB) != 0)
        throw ValidationError("partition table device is locked by another process");

    OwnedContext context(fdisk_new_context());
    if (!context)
        throw ValidationError("cannot allocate libfdisk context");
    require_success(
        fdisk_assign_device_by_fd(context.get(), descriptor.get(), device.c_str(), 1),
        "assigning partition table device"
    );
    try {
        require_success(fdisk_disable_dialogs(context.get(), 1), "disabling libfdisk dialogs");
        validate_gpt_identity(context.get(), expected_partition_table_id, expected_logical_sector_size);
        fdisk_table* raw_partitions = nullptr;
        require_success(fdisk_get_partitions(context.get(), &raw_partitions), "reading GPT partitions");
        OwnedTable partitions(raw_partitions);
        bool exact_partition = false;
        bool conflict = false;
        for (std::size_t index = 0; index < fdisk_table_get_nents(partitions.get()); ++index) {
            fdisk_partition* partition = fdisk_table_get_partition(partitions.get(), index);
            if (partition == nullptr || !fdisk_partition_has_partno(partition) ||
                !fdisk_partition_has_start(partition) || !fdisk_partition_has_size(partition))
                continue;
            const std::size_t partno = fdisk_partition_get_partno(partition) + 1;
            const std::uint64_t start = fdisk_partition_get_start(partition);
            const std::uint64_t size = fdisk_partition_get_size(partition);
            const bool exact = partno == geometry.partition_number && start == geometry.start_sector &&
                size == geometry.sector_count;
            exact_partition = exact_partition || exact;
            conflict = conflict || (!exact && (partno == geometry.partition_number || regions_overlap(start, size, geometry.start_sector, geometry.sector_count)));
        }

        PartitionCreationInspection result;
        if (exact_partition && !conflict) {
            const std::uint64_t sector_ratio = expected_logical_sector_size / 512;
            if (expected_logical_sector_size % 512 != 0 || sector_ratio == 0 ||
                geometry.start_sector > std::numeric_limits<std::uint64_t>::max() / sector_ratio ||
                geometry.sector_count > std::numeric_limits<std::uint64_t>::max() / sector_ratio)
                throw ValidationError("partition table sector size is unsupported");
            OwnedUdev udev_context(udev_new());
            if (!udev_context)
                throw ValidationError("cannot initialize udev partition inspection");
            const auto partition_path = find_exact_partition(
                udev_context.get(),
                status.st_rdev,
                geometry.partition_number,
                geometry.start_sector * sector_ratio,
                geometry.sector_count * sector_ratio
            );
            result.state = partition_path.has_value()
                ? PartitionCreationState::Created
                : PartitionCreationState::Conflict;
            if (partition_path.has_value())
                result.partition = *partition_path;
        } else if (!conflict && has_exact_free_region(context.get(), original_free_start_sector, original_free_sector_count)) {
            result.state = PartitionCreationState::NotCreated;
        } else {
            result.state = PartitionCreationState::Conflict;
        }
        require_success(fdisk_deassign_device(context.get(), 1), "releasing partition table device");
        return result;
    } catch (...) {
        if (fdisk_get_devfd(context.get()) >= 0)
            static_cast<void>(fdisk_deassign_device(context.get(), 1));
        throw;
    }
}

std::filesystem::path LibfdiskPartitionTableOperations::create_partition_in_free_space(
    const std::filesystem::path& device,
    const std::string& expected_major_minor,
    const std::string& expected_partition_table_id,
    std::uint32_t expected_logical_sector_size,
    std::uint64_t free_start_sector,
    std::uint64_t free_sector_count,
    const PlannedPartitionGeometry& geometry
) {
    validate_device_path(device);
    validate_free_space_request(
        expected_partition_table_id,
        expected_logical_sector_size,
        free_start_sector,
        free_sector_count
    );
    const std::uint64_t free_end = free_start_sector + free_sector_count - 1;
    if (geometry.partition_number == 0 || geometry.sector_count == 0 ||
        geometry.start_sector < free_start_sector || geometry.start_sector > free_end ||
        geometry.sector_count > free_end - geometry.start_sector + 1)
        throw ValidationError("planned partition geometry exceeds the selected free region");
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
        validate_gpt_free_region(
            context.get(),
            expected_partition_table_id,
            expected_logical_sector_size,
            free_start_sector,
            free_sector_count
        );
        if (fdisk_lba_is_phy_aligned(context.get(), geometry.start_sector) != 1)
            throw ValidationError("planned partition start is not physically aligned");
        OwnedPartition partition(fdisk_new_partition());
        if (!partition)
            throw ValidationError("cannot allocate libfdisk partition");
        require_success(fdisk_partition_set_start(partition.get(), geometry.start_sector), "selecting partition start");
        require_success(fdisk_partition_set_size(partition.get(), geometry.sector_count), "selecting partition size");
        require_success(fdisk_partition_size_explicit(partition.get(), 1), "fixing partition size");
        require_success(
            fdisk_partition_set_partno(partition.get(), static_cast<std::size_t>(geometry.partition_number - 1)),
            "selecting partition number"
        );
        std::size_t partition_number = 0;
        require_success(fdisk_add_partition(context.get(), partition.get(), &partition_number), "adding GPT partition");
        if (partition_number + 1 != geometry.partition_number ||
            fdisk_partition_get_start(partition.get()) != geometry.start_sector ||
            fdisk_partition_get_size(partition.get()) != geometry.sector_count)
            throw ValidationError("libfdisk changed planned partition geometry");
        const std::uint64_t logical_sector_size = fdisk_get_sector_size(context.get());
        if (logical_sector_size == 0 || logical_sector_size % 512 != 0)
            throw ValidationError("partition table sector size is unsupported");
        const std::uint64_t sysfs_sector_ratio = logical_sector_size / 512;
        if (geometry.start_sector > std::numeric_limits<std::uint64_t>::max() / sysfs_sector_ratio ||
            geometry.sector_count > std::numeric_limits<std::uint64_t>::max() / sysfs_sector_ratio)
            throw ValidationError("partition geometry exceeds supported range");
        PartitionAppearanceMonitor appearance(status.st_rdev);
        require_success(fdisk_write_disklabel(context.get()), "writing GPT partition table");
        if (::fsync(descriptor.get()) != 0)
            throw ValidationError("syncing GPT partition table failed");
        require_success(fdisk_reread_partition_table(context.get()), "rereading GPT partition table");
        const auto partition_path = appearance.wait_for(
            geometry.partition_number,
            geometry.start_sector * sysfs_sector_ratio,
            geometry.sector_count * sysfs_sector_ratio,
            std::chrono::seconds(10)
        );
        require_success(fdisk_deassign_device(context.get(), 0), "releasing partition table device");
        return partition_path;
    } catch (...) {
        if (fdisk_get_devfd(context.get()) >= 0)
            static_cast<void>(fdisk_deassign_device(context.get(), 1));
        throw;
    }
}

std::filesystem::path LibfdiskPartitionTableOperations::replace_with_single_gpt_partition(
    const std::filesystem::path& device,
    const std::string& expected_major_minor
) {
    validate_device_path(device);
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
        const auto partition_path = appearance.wait_for(
            partition_number + 1,
            partition_start * sysfs_sector_ratio,
            partition_size * sysfs_sector_ratio,
            std::chrono::seconds(10)
        );
        require_success(fdisk_deassign_device(context.get(), 0), "releasing partition table device");
        return partition_path;
    } catch (...) {
        if (fdisk_get_devfd(context.get()) >= 0)
            static_cast<void>(fdisk_deassign_device(context.get(), 1));
        throw;
    }
}

} // namespace btrfsbackup::platform::linux::storage
