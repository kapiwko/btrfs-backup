// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/SignatureOperations.hpp>

#include <blkid/blkid.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <memory>
#include <string>

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

std::string device_number(const struct stat& info) {
    return std::to_string(major(info.st_rdev)) + ":" + std::to_string(minor(info.st_rdev));
}

std::string probe_value(blkid_probe probe, const char* name) {
    const char* value = nullptr;
    std::size_t size = 0;
    if (blkid_probe_lookup_value(probe, name, &value, &size) != 0 || value == nullptr)
        return {};
    if (size > 0 && value[size - 1] == '\0')
        --size;
    return std::string(value, size);
}

SignatureExpectation read_signature(int descriptor) {
    OwnedProbe probe(blkid_new_probe());
    if (!probe || blkid_probe_set_device(probe.get(), descriptor, 0, 0) != 0 ||
        blkid_probe_enable_superblocks(probe.get(), 1) != 0 ||
        blkid_probe_set_superblocks_flags(
            probe.get(),
            BLKID_SUBLKS_TYPE | BLKID_SUBLKS_VERSION | BLKID_SUBLKS_LABEL | BLKID_SUBLKS_UUID
        ) != 0)
        throw ValidationError("cannot inspect signature target");
    const int result = blkid_do_safeprobe(probe.get());
    if (result == 1)
        return {};
    if (result != 0)
        throw ValidationError("signature target state is ambiguous");
    return {
        .type = probe_value(probe.get(), "TYPE"),
        .version = probe_value(probe.get(), "VERSION"),
        .label = probe_value(probe.get(), "LABEL"),
        .uuid = probe_value(probe.get(), "UUID"),
    };
}

} // namespace

void LibblkidSignatureOperations::wipe_all(
    const std::filesystem::path& device,
    const std::string& expected_major_minor,
    const std::optional<SignatureExpectation>& expected_signature
) {
    if (!device.is_absolute() || device.lexically_normal() != device || expected_major_minor.empty())
        throw ValidationError("signature target identity is invalid");

    OwnedFileDescriptor descriptor(::open(device.c_str(), O_RDWR | O_CLOEXEC | O_EXCL | O_NOFOLLOW));
    if (!descriptor.valid())
        throw ValidationError("cannot exclusively open signature target");

    struct stat info{};
    if (::fstat(descriptor.get(), &info) != 0 || !S_ISBLK(info.st_mode) ||
        device_number(info) != expected_major_minor)
        throw ValidationError("signature target identity changed");
    if (expected_signature.has_value() && read_signature(descriptor.get()) != *expected_signature)
        throw ValidationError("signature target content changed");

    OwnedProbe probe(blkid_new_probe());
    if (!probe || blkid_probe_set_device(probe.get(), descriptor.get(), 0, 0) != 0)
        throw ValidationError("cannot initialize signature erasure");
    if (blkid_wipe_all(probe.get()) != 0)
        throw ValidationError("erasing storage signatures failed");
    if (::fsync(descriptor.get()) != 0)
        throw ValidationError("synchronizing erased storage signatures failed");
}

} // namespace btrfsbackup::platform::linux::storage
