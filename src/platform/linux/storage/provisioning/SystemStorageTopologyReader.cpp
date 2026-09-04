// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/provisioning/SystemStorageTopologyReader.hpp>

#include <blkid/blkid.h>
#include <libfdisk/libfdisk.h>
#include <libmount/libmount.h>
#include <libudev.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <core/Errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux::storage::provisioning {
namespace {

using ::btrfsbackup::provisioning::ExistingPartition;
using ::btrfsbackup::provisioning::FilesystemDescription;
using ::btrfsbackup::provisioning::PartitionTableType;
using ::btrfsbackup::provisioning::SafetyBlocker;
using ::btrfsbackup::provisioning::StableBlockDeviceIdentity;
using ::btrfsbackup::provisioning::StorageDevice;
using ::btrfsbackup::provisioning::StorageRegion;
using ::btrfsbackup::provisioning::StorageTopology;
using ::btrfsbackup::provisioning::UnallocatedRegion;

template <typename T, auto Release>
struct PointerDeleter {
    void operator()(T* value) const noexcept {
        if (value != nullptr)
            static_cast<void>(Release(value));
    }
};

using Udev = std::unique_ptr<udev, PointerDeleter<udev, udev_unref>>;
using UdevEnumerate = std::unique_ptr<udev_enumerate, PointerDeleter<udev_enumerate, udev_enumerate_unref>>;
using UdevDevice = std::unique_ptr<udev_device, PointerDeleter<udev_device, udev_device_unref>>;
using BlkidProbe = std::unique_ptr<blkid_struct_probe, PointerDeleter<blkid_struct_probe, blkid_free_probe>>;
using FdiskContext = std::unique_ptr<fdisk_context, PointerDeleter<fdisk_context, fdisk_unref_context>>;
using FdiskTable = std::unique_ptr<fdisk_table, PointerDeleter<fdisk_table, fdisk_unref_table>>;
using MountTable = std::unique_ptr<libmnt_table, PointerDeleter<libmnt_table, mnt_free_table>>;
using MountIter = std::unique_ptr<libmnt_iter, PointerDeleter<libmnt_iter, mnt_free_iter>>;

std::string value_or_empty(const char* value) {
    return value == nullptr ? std::string{} : std::string(value);
}

std::string property(udev_device* device, const char* name) {
    return value_or_empty(udev_device_get_property_value(device, name));
}

std::optional<std::uint64_t> read_unsigned(const fs::path& path) {
    std::ifstream input(path);
    std::uint64_t value = 0;
    if (!(input >> value))
        return std::nullopt;
    return value;
}

std::uint64_t checked_bytes(std::uint64_t sectors, std::uint64_t sector_size) {
    if (sector_size == 0 || sectors > std::numeric_limits<std::uint64_t>::max() / sector_size)
        return 0;
    return sectors * sector_size;
}

std::string major_minor(dev_t number) {
    return std::to_string(major(number)) + ":" + std::to_string(minor(number));
}

bool equal_ignoring_ascii_case(std::string_view left, std::string_view right) {
    return left.size() == right.size() && std::ranges::equal(left, right, [](char lhs, char rhs) {
               return std::tolower(static_cast<unsigned char>(lhs)) ==
                   std::tolower(static_cast<unsigned char>(rhs));
           });
}

void add_blocker(std::vector<SafetyBlocker>& blockers, std::string code, std::string detail = {}) {
    const auto duplicate = std::ranges::find_if(blockers, [&](const auto& blocker) {
        return blocker.code == code && blocker.detail == detail;
    });
    if (duplicate == blockers.end())
        blockers.push_back({std::move(code), std::move(detail)});
}

std::vector<std::string> directory_entries(const fs::path& path, bool& available) {
    std::vector<std::string> result;
    std::error_code error;
    if (!fs::is_directory(path, error) || error) {
        available = false;
        return result;
    }
    available = true;
    for (fs::directory_iterator item(path, error); !error && item != fs::directory_iterator{}; item.increment(error))
        result.push_back(item->path().filename().string());
    if (error) {
        available = false;
        result.clear();
        return result;
    }
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

struct ProbeResult {
    FilesystemDescription filesystem;
    std::vector<SafetyBlocker> blockers;
};

std::string probe_value(blkid_probe probe, const char* name) {
    const char* value = nullptr;
    std::size_t size = 0;
    if (blkid_probe_lookup_value(probe, name, &value, &size) != 0 || value == nullptr)
        return {};
    if (size > 0 && value[size - 1] == '\0')
        --size;
    return std::string(value, size);
}

ProbeResult probe_filesystem(const std::string& path) {
    ProbeResult result;
    if (path.empty()) {
        add_blocker(result.blockers, "missing-device-node");
        return result;
    }
    BlkidProbe probe(blkid_new_probe_from_filename(path.c_str()));
    if (!probe) {
        add_blocker(result.blockers, "signature-state-unavailable", path);
        return result;
    }
    blkid_probe_enable_superblocks(probe.get(), 1);
    blkid_probe_set_superblocks_flags(
        probe.get(),
        BLKID_SUBLKS_TYPE | BLKID_SUBLKS_UUID | BLKID_SUBLKS_LABEL | BLKID_SUBLKS_VERSION
    );
    const int status = blkid_do_safeprobe(probe.get());
    if (status == 1)
        return result;
    if (status != 0) {
        add_blocker(result.blockers, status == -2 ? "ambiguous-signatures" : "signature-state-unavailable", path);
        return result;
    }
    result.filesystem.type = probe_value(probe.get(), "TYPE");
    result.filesystem.version = probe_value(probe.get(), "VERSION");
    result.filesystem.label = probe_value(probe.get(), "LABEL");
    result.filesystem.uuid = probe_value(probe.get(), "UUID");
    return result;
}

struct RawNode {
    dev_t number = 0;
    dev_t parent = 0;
    std::string path;
    std::string syspath;
    std::string devtype;
    std::string sysname;
    std::string model;
    std::string transport;
    std::string wwn;
    std::string serial;
    std::string serial_short;
    std::string partition_uuid;
    std::string partition_label;
    std::uint64_t size_bytes = 0;
    std::uint64_t start_sector = 0;
    std::uint32_t partition_number = 0;
    std::uint32_t logical_sector_size = 0;
    std::uint32_t physical_sector_size = 0;
    bool removable = false;
    bool read_only = false;
    bool hotplug = false;
    bool active_swap = false;
    std::vector<std::string> mount_points;
    std::vector<std::string> holders;
    std::vector<std::string> slaves;
    std::vector<SafetyBlocker> blockers;
};

std::string detect_transport(udev_device* device) {
    std::string result = property(device, "ID_BUS");
    for (udev_device* current = udev_device_get_parent(device); result.empty() && current != nullptr;
         current = udev_device_get_parent(current)) {
        const std::string subsystem = value_or_empty(udev_device_get_subsystem(current));
        if (subsystem == "usb" || subsystem == "nvme" || subsystem == "scsi" ||
            subsystem == "virtio" || subsystem == "mmc")
            result = subsystem;
    }
    return result;
}

std::map<dev_t, std::vector<std::string>> read_mounts(const fs::path& mountinfo) {
    MountTable table(mnt_new_table_from_file(mountinfo.c_str()));
    if (!table)
        throw ValidationError("cannot read mount topology");
    MountIter iterator(mnt_new_iter(MNT_ITER_FORWARD));
    if (!iterator)
        throw ValidationError("cannot allocate mount iterator");
    std::map<dev_t, std::vector<std::string>> result;
    libmnt_fs* entry = nullptr;
    while (mnt_table_next_fs(table.get(), iterator.get(), &entry) == 0) {
        dev_t number = mnt_fs_get_devno(entry);
        const char* source = mnt_fs_get_srcpath(entry);
        if (major(number) == 0 && source != nullptr && std::string_view(source).starts_with("/dev/")) {
            struct stat source_status{};
            if (::stat(source, &source_status) == 0 && S_ISBLK(source_status.st_mode))
                number = source_status.st_rdev;
        }
        const char* target = mnt_fs_get_target(entry);
        if (number != 0 && target != nullptr && *target != '\0')
            result[number].emplace_back(target);
    }
    for (auto& [number, targets] : result) {
        static_cast<void>(number);
        std::ranges::sort(targets);
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    }
    return result;
}

std::set<dev_t> read_swaps(const fs::path& swaps) {
    std::ifstream input(swaps);
    if (!input)
        throw ValidationError("cannot read swap topology");
    std::string line;
    static_cast<void>(std::getline(input, line));
    std::set<dev_t> result;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string path;
        fields >> path;
        struct stat info{};
        if (!path.empty() && ::stat(path.c_str(), &info) == 0 && S_ISBLK(info.st_mode))
            result.insert(info.st_rdev);
    }
    return result;
}

std::map<dev_t, RawNode> enumerate_nodes(
    const fs::path& sysfs_root,
    const std::map<dev_t, std::vector<std::string>>& mounts,
    const std::set<dev_t>& swaps
) {
    Udev context(udev_new());
    if (!context)
        throw ValidationError("cannot initialize libudev");
    UdevEnumerate enumerate(udev_enumerate_new(context.get()));
    if (!enumerate || udev_enumerate_add_match_subsystem(enumerate.get(), "block") < 0 ||
        udev_enumerate_scan_devices(enumerate.get()) < 0)
        throw ValidationError("cannot enumerate block devices with libudev");

    std::map<dev_t, RawNode> result;
    udev_list_entry* item = nullptr;
    udev_list_entry_foreach(item, udev_enumerate_get_list_entry(enumerate.get())) {
        const char* syspath = udev_list_entry_get_name(item);
        UdevDevice device(udev_device_new_from_syspath(context.get(), syspath));
        if (!device)
            continue;
        const dev_t number = udev_device_get_devnum(device.get());
        if (number == 0)
            continue;
        RawNode node;
        node.number = number;
        node.path = value_or_empty(udev_device_get_devnode(device.get()));
        node.syspath = value_or_empty(udev_device_get_syspath(device.get()));
        node.devtype = value_or_empty(udev_device_get_devtype(device.get()));
        node.sysname = value_or_empty(udev_device_get_sysname(device.get()));
        node.model = property(device.get(), "ID_MODEL");
        node.transport = detect_transport(device.get());
        node.wwn = property(device.get(), "ID_WWN_WITH_EXTENSION");
        if (node.wwn.empty())
            node.wwn = property(device.get(), "ID_WWN");
        node.serial = property(device.get(), "ID_SERIAL");
        node.serial_short = property(device.get(), "ID_SERIAL_SHORT");
        node.partition_uuid = property(device.get(), "ID_PART_ENTRY_UUID");
        node.partition_label = property(device.get(), "ID_PART_ENTRY_NAME");
        if (udev_device* parent = udev_device_get_parent_with_subsystem_devtype(device.get(), "block", "disk"))
            node.parent = udev_device_get_devnum(parent);

        const fs::path block_path = sysfs_root / "dev/block" / major_minor(number);
        const auto sectors = read_unsigned(block_path / "size");
        node.start_sector = read_unsigned(block_path / "start").value_or(0);
        node.partition_number = static_cast<std::uint32_t>(read_unsigned(block_path / "partition").value_or(0));
        node.logical_sector_size = static_cast<std::uint32_t>(
            read_unsigned(block_path / "queue/logical_block_size").value_or(0)
        );
        node.physical_sector_size = static_cast<std::uint32_t>(
            read_unsigned(block_path / "queue/physical_block_size").value_or(0)
        );
        if (node.logical_sector_size == 0 && node.parent != 0) {
            const fs::path parent_path = sysfs_root / "dev/block" / major_minor(node.parent);
            node.logical_sector_size = static_cast<std::uint32_t>(
                read_unsigned(parent_path / "queue/logical_block_size").value_or(0)
            );
            node.physical_sector_size = static_cast<std::uint32_t>(
                read_unsigned(parent_path / "queue/physical_block_size").value_or(0)
            );
        }
        node.size_bytes = sectors.has_value() ? checked_bytes(*sectors, 512) : 0;
        node.removable = read_unsigned(block_path / "removable").value_or(0) != 0;
        node.read_only = read_unsigned(block_path / "ro").value_or(0) != 0;
        node.hotplug = node.removable || property(device.get(), "ID_DRIVE_FLASH").empty() == false;
        if (const auto found = mounts.find(number); found != mounts.end())
            node.mount_points = found->second;
        node.active_swap = swaps.contains(number);
        bool holders_available = false;
        node.holders = directory_entries(block_path / "holders", holders_available);
        if (!holders_available)
            add_blocker(node.blockers, "holder-state-unavailable", major_minor(number));
        if (node.devtype == "disk") {
            bool slaves_available = false;
            node.slaves = directory_entries(block_path / "slaves", slaves_available);
            if (!slaves_available)
                add_blocker(node.blockers, "dependency-state-unavailable", major_minor(number));
        }
        if (node.path.empty())
            add_blocker(node.blockers, "missing-device-node", major_minor(number));
        if (node.size_bytes == 0)
            add_blocker(node.blockers, "device-size-unavailable", major_minor(number));
        if (node.devtype != "disk" && node.devtype != "partition")
            add_blocker(node.blockers, "unsupported-block-stack", node.devtype);
        result.insert_or_assign(number, std::move(node));
    }
    return result;
}

StableBlockDeviceIdentity identity(const RawNode& node) {
    return {
        .display_path = node.path,
        .major_minor = major_minor(node.number),
        .sysfs_path = node.syspath,
        .wwn = node.wwn,
        .serial = node.serial,
        .serial_short = node.serial_short,
        .size_bytes = node.size_bytes,
    };
}

std::optional<std::reference_wrapper<const RawNode>> partition_node(
    const std::map<dev_t, RawNode>& nodes,
    dev_t parent,
    std::uint32_t number
) {
    const auto found = std::ranges::find_if(nodes, [&](const auto& entry) {
        return entry.second.devtype == "partition" && entry.second.parent == parent &&
            entry.second.partition_number == number;
    });
    if (found == nodes.end())
        return std::nullopt;
    return std::cref(found->second);
}

void merge_blockers(std::vector<SafetyBlocker>& destination, const std::vector<SafetyBlocker>& source) {
    for (const auto& blocker : source)
        add_blocker(destination, blocker.code, blocker.detail);
}

std::set<dev_t> system_device_numbers(const std::map<dev_t, RawNode>& nodes) {
    std::map<std::string, dev_t> numbers_by_name;
    std::vector<dev_t> pending;
    for (const auto& [number, node] : nodes) {
        numbers_by_name.insert_or_assign(node.sysname, number);
        if (std::ranges::any_of(node.mount_points, [](const std::string& mount_point) {
                return mount_point == "/" || mount_point == "/boot" || mount_point == "/boot/efi" ||
                    mount_point == "/home";
            }))
            pending.push_back(number);
    }

    std::set<dev_t> result;
    while (!pending.empty()) {
        const dev_t number = pending.back();
        pending.pop_back();
        if (!result.insert(number).second)
            continue;
        const auto node = nodes.find(number);
        if (node == nodes.end())
            continue;
        if (node->second.parent != 0)
            pending.push_back(node->second.parent);
        for (const auto& slave : node->second.slaves) {
            if (const auto found = numbers_by_name.find(slave); found != numbers_by_name.end())
                pending.push_back(found->second);
        }
    }
    return result;
}

StorageDevice describe_disk(
    const RawNode& disk,
    const std::map<dev_t, RawNode>& nodes,
    const std::set<dev_t>& system_devices
) {
    StorageDevice result;
    result.identity = identity(disk);
    result.candidate_id = "device-" + result.identity.major_minor;
    result.display_name = disk.model.empty() ? disk.sysname : disk.model;
    result.transport = disk.transport;
    result.size_bytes = disk.size_bytes;
    result.logical_sector_size = disk.logical_sector_size;
    result.physical_sector_size = disk.physical_sector_size;
    result.removable = disk.removable;
    result.read_only = disk.read_only;
    result.hotplug = disk.hotplug;
    result.system_device = system_devices.contains(disk.number);
    result.mount_points = disk.mount_points;
    result.holders = disk.holders;
    result.active_swap = disk.active_swap;
    result.blockers = disk.blockers;
    if (disk.read_only)
        add_blocker(result.blockers, "read-only-device");
    if (!disk.mount_points.empty())
        add_blocker(result.blockers, "mounted-filesystem", disk.mount_points.front());
    if (disk.active_swap)
        add_blocker(result.blockers, "active-swap");
    if (!disk.holders.empty() || !disk.slaves.empty())
        add_blocker(result.blockers, "unsupported-block-stack");
    if (disk.transport.empty())
        add_blocker(result.blockers, "transport-unavailable");
    if (disk.wwn.empty() && disk.serial.empty() && disk.serial_short.empty())
        add_blocker(result.blockers, "stable-identity-unavailable");

    ProbeResult disk_probe = probe_filesystem(disk.path);
    result.filesystem = std::move(disk_probe.filesystem);
    merge_blockers(result.blockers, disk_probe.blockers);

    FdiskContext context(fdisk_new_context());
    if (!context || fdisk_assign_device(context.get(), disk.path.c_str(), 1) != 0) {
        add_blocker(result.blockers, "partition-table-unavailable");
        return result;
    }
    const auto sector_size = fdisk_get_sector_size(context.get());
    const auto physical_sector_size = fdisk_get_physector_size(context.get());
    if (sector_size != 0)
        result.logical_sector_size = static_cast<std::uint32_t>(sector_size);
    if (physical_sector_size != 0)
        result.physical_sector_size = static_cast<std::uint32_t>(physical_sector_size);
    if (result.logical_sector_size == 0)
        add_blocker(result.blockers, "sector-size-unavailable");

    if (!fdisk_has_label(context.get())) {
        result.partition_table.type = PartitionTableType::None;
        return result;
    }
    if (fdisk_is_label(context.get(), GPT))
        result.partition_table.type = PartitionTableType::Gpt;
    else if (fdisk_is_label(context.get(), DOS))
        result.partition_table.type = PartitionTableType::Mbr;
    else
        result.partition_table.type = PartitionTableType::Unsupported;
    char* raw_identifier = nullptr;
    if (fdisk_get_disklabel_id(context.get(), &raw_identifier) == 0 && raw_identifier != nullptr) {
        result.partition_table.identifier = raw_identifier;
        std::free(raw_identifier);
    }

    fdisk_table* raw_partitions = nullptr;
    if (fdisk_get_partitions(context.get(), &raw_partitions) != 0) {
        add_blocker(result.blockers, "partition-table-unavailable");
        return result;
    }
    FdiskTable partitions(raw_partitions);
    for (std::size_t index = 0; index < fdisk_table_get_nents(partitions.get()); ++index) {
        fdisk_partition* partition = fdisk_table_get_partition(partitions.get(), index);
        if (partition == nullptr || !fdisk_partition_has_partno(partition) ||
            !fdisk_partition_has_start(partition) || !fdisk_partition_has_size(partition))
            continue;
        ExistingPartition region;
        region.partition_number = static_cast<std::uint32_t>(fdisk_partition_get_partno(partition) + 1);
        region.start_sector = fdisk_partition_get_start(partition);
        region.sector_count = fdisk_partition_get_size(partition);
        const char* uuid = fdisk_partition_get_uuid(partition);
        const char* label = fdisk_partition_get_name(partition);
        if (uuid != nullptr && *uuid != '\0')
            region.partition_uuid = uuid;
        if (label != nullptr && *label != '\0')
            region.partition_label = label;
        const auto raw = partition_node(nodes, disk.number, region.partition_number);
        if (!raw.has_value()) {
            region.identity.major_minor = "missing";
            region.identity.size_bytes = checked_bytes(region.sector_count, result.logical_sector_size);
            add_blocker(region.blockers, "partition-device-node-missing", std::to_string(region.partition_number));
        } else {
            const RawNode& node = raw->get();
            region.identity = identity(node);
            region.mount_points = node.mount_points;
            region.holders = node.holders;
            region.active_swap = node.active_swap;
            merge_blockers(region.blockers, node.blockers);
            if (node.start_sector != region.start_sector ||
                node.size_bytes != checked_bytes(region.sector_count, result.logical_sector_size))
                add_blocker(region.blockers, "partition-geometry-conflict");
            if (!node.partition_uuid.empty()) {
                if (region.partition_uuid.has_value() &&
                    !equal_ignoring_ascii_case(*region.partition_uuid, node.partition_uuid))
                    add_blocker(region.blockers, "partition-uuid-conflict");
                region.partition_uuid = node.partition_uuid;
            }
            if (!node.partition_label.empty())
                region.partition_label = node.partition_label;
            ProbeResult filesystem = probe_filesystem(node.path);
            region.filesystem = std::move(filesystem.filesystem);
            merge_blockers(region.blockers, filesystem.blockers);
        }
        if (!region.mount_points.empty())
            add_blocker(region.blockers, "mounted-filesystem", region.mount_points.front());
        if (region.active_swap)
            add_blocker(region.blockers, "active-swap");
        if (!region.holders.empty())
            add_blocker(region.blockers, "block-holder", region.holders.front());
        region.candidate_id = "partition-" + result.identity.major_minor + "-" +
            std::to_string(region.partition_number) + "-" + std::to_string(region.start_sector);
        region.suitable_for_reformat = region.blockers.empty();
        region.suitable_for_adoption = region.blockers.empty() && region.filesystem.type == "crypto_LUKS";
        result.regions.emplace_back(std::move(region));
    }

    fdisk_table* raw_free = nullptr;
    if (fdisk_get_freespaces(context.get(), &raw_free) != 0) {
        add_blocker(result.blockers, "free-space-unavailable");
    } else {
        FdiskTable free_spaces(raw_free);
        for (std::size_t index = 0; index < fdisk_table_get_nents(free_spaces.get()); ++index) {
            fdisk_partition* free = fdisk_table_get_partition(free_spaces.get(), index);
            if (free == nullptr || !fdisk_partition_has_start(free) || !fdisk_partition_has_size(free))
                continue;
            UnallocatedRegion region;
            region.start_sector = fdisk_partition_get_start(free);
            region.sector_count = fdisk_partition_get_size(free);
            region.id = "free-" + result.identity.major_minor + "-" + std::to_string(region.start_sector) +
                "-" + std::to_string(region.sector_count);
            if (result.partition_table.type != PartitionTableType::Gpt)
                add_blocker(region.blockers, "unsupported-partition-table");
            region.suitable_for_backup_partition = region.blockers.empty();
            result.regions.emplace_back(std::move(region));
        }
    }
    std::ranges::sort(result.regions, {}, ::btrfsbackup::provisioning::region_start_sector);
    return result;
}

std::string topology_fingerprint(const StorageTopology& topology) {
    std::ostringstream canonical;
    const auto append_strings = [&](const std::vector<std::string>& values) {
        for (const auto& value : values)
            canonical << value << '\x1f';
        canonical << '\0';
    };
    const auto append_blockers = [&](const std::vector<SafetyBlocker>& blockers) {
        for (const auto& blocker : blockers)
            canonical << blocker.code << ':' << blocker.detail << '\x1f';
        canonical << '\0';
    };
    for (const auto& device : topology.devices) {
        canonical << device.identity.major_minor << '\0' << device.identity.sysfs_path << '\0'
                  << device.identity.wwn << '\0' << device.identity.serial << '\0'
                  << device.identity.serial_short << '\0' << device.size_bytes << '\0'
                  << device.logical_sector_size << '\0' << device.physical_sector_size << '\0'
                  << device.system_device << '\0'
                  << ::btrfsbackup::provisioning::partition_table_type_name(device.partition_table.type) << '\0'
                  << device.partition_table.identifier << '\0';
        append_strings(device.mount_points);
        append_strings(device.holders);
        canonical << device.active_swap << '\0' << device.read_only << '\0';
        append_blockers(device.blockers);
        for (const auto& region : device.regions) {
            std::visit(
                [&](const auto& value) {
                    canonical << value.start_sector << ':' << value.sector_count << ':';
                    if constexpr (std::is_same_v<std::decay_t<decltype(value)>, ExistingPartition>) {
                        canonical << value.identity.major_minor << ':' << value.partition_number << ':'
                                  << value.partition_uuid.value_or("") << ':' << value.filesystem.type << ':'
                                  << value.filesystem.uuid;
                        canonical << ':' << value.identity.size_bytes << ':' << value.active_swap;
                        append_strings(value.mount_points);
                        append_strings(value.holders);
                        append_blockers(value.blockers);
                    } else {
                        canonical << value.id;
                        append_blockers(value.blockers);
                    }
                    canonical << '\0';
                },
                region
            );
        }
    }
    std::uint64_t hash = 1469598103934665603ULL;
    const std::string canonical_bytes = canonical.str();
    for (const char byte : canonical_bytes) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    std::ostringstream result;
    result << "topology-" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return result.str();
}

} // namespace

SystemStorageTopologyReader::SystemStorageTopologyReader(
    SystemStorageTopologyPaths paths,
    ConfiguredBackupTargetProvider configured_targets
) : paths_(std::move(paths)),
    configured_targets_(configured_targets ? std::move(configured_targets) : ConfiguredBackupTargetProvider{[] {
        return std::vector<::btrfsbackup::provisioning::ConfiguredBackupTargetIdentity>{};
    }}) {
    if (!paths_.sysfs_root.is_absolute() || !paths_.mountinfo.is_absolute() || !paths_.swaps.is_absolute())
        throw std::invalid_argument("storage topology paths must be absolute");
}

StorageTopology SystemStorageTopologyReader::scan() {
    const auto mounts = read_mounts(paths_.mountinfo);
    const auto swaps = read_swaps(paths_.swaps);
    const auto nodes = enumerate_nodes(paths_.sysfs_root, mounts, swaps);
    const auto system_devices = system_device_numbers(nodes);
    StorageTopology result;
    for (const auto& [number, node] : nodes) {
        static_cast<void>(number);
        if (node.devtype == "disk" && node.parent == 0)
            result.devices.push_back(describe_disk(node, nodes, system_devices));
    }
    ::btrfsbackup::provisioning::ConfiguredBackupTargetMarker(configured_targets_()).apply(result);
    std::ranges::sort(result.devices, {}, [](const auto& device) { return device.identity.major_minor; });
    result.generation = topology_fingerprint(result);
    return result;
}

} // namespace btrfsbackup::platform::linux::storage::provisioning
