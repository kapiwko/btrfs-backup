// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/BlockDeviceMetadata.hpp>

#include <blkid/blkid.h>
#include <fcntl.h>

#include <memory>

#include <core/Errors.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>

namespace btrfsbackup::platform::linux::storage {
namespace {

struct ProbeDeleter {
    void operator()(blkid_probe probe) const noexcept {
        blkid_free_probe(probe);
    }
};

using OwnedProbe = std::unique_ptr<blkid_struct_probe, ProbeDeleter>;

std::string value(blkid_probe probe, const char* name) {
    const char* data = nullptr;
    std::size_t size = 0;
    if (blkid_probe_lookup_value(probe, name, &data, &size) != 0 || data == nullptr)
        return {};
    if (size > 0 && data[size - 1] == '\0')
        --size;
    return {data, size};
}

} // namespace

BlockDeviceMetadata LibblkidBlockDeviceMetadataReader::read(const std::filesystem::path& device) {
    if (!device.is_absolute() || device.lexically_normal() != device)
        throw ValidationError("block device metadata path is invalid");
    OwnedFileDescriptor descriptor(::open(device.c_str(), O_RDONLY | O_CLOEXEC));
    if (!descriptor.valid())
        throw ValidationError("cannot open block device metadata target");
    OwnedProbe probe(blkid_new_probe());
    if (!probe || blkid_probe_set_device(probe.get(), descriptor.get(), 0, 0) != 0)
        throw ValidationError("cannot initialize block device metadata probe");
    blkid_probe_enable_superblocks(probe.get(), 1);
    blkid_probe_set_superblocks_flags(probe.get(), BLKID_SUBLKS_UUID | BLKID_SUBLKS_TYPE);
    blkid_probe_enable_partitions(probe.get(), 1);
    blkid_probe_set_partitions_flags(probe.get(), BLKID_PARTS_ENTRY_DETAILS);
    const int result = blkid_do_safeprobe(probe.get());
    if (result != 0)
        throw ValidationError(result == 1 ? "block device metadata is unavailable" : "block device metadata probe failed");
    return {
        .filesystem_uuid = value(probe.get(), "UUID"),
        .partition_uuid = value(probe.get(), "PART_ENTRY_UUID"),
    };
}

} // namespace btrfsbackup::platform::linux::storage
