// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DestructiveDeviceSafetyInspector.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <set>
#include <utility>

#include <daemon/provisioning/StorageTopologyReader.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

using platform::linux::OwnedFileDescriptor;
using provisioning::ExistingPartition;

void add_reason(std::vector<std::string>& reasons, std::string reason) {
    if (std::ranges::find(reasons, reason) == reasons.end())
        reasons.push_back(std::move(reason));
}

bool system_mount(const std::string& mountpoint) {
    static const std::set<std::string> system_mounts{"/", "/boot", "/boot/efi", "/home", "/usr", "/var"};
    return system_mounts.contains(mountpoint);
}

std::optional<std::string> device_number(const fs::path& path) {
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0 || !S_ISBLK(info.st_mode))
        return std::nullopt;
    return std::to_string(major(info.st_rdev)) + ":" + std::to_string(minor(info.st_rdev));
}

std::optional<std::string> probe_exclusive_open(const ProvisioningDevice& device) {
    OwnedFileDescriptor descriptor(::open(device.path.c_str(), O_RDWR | O_CLOEXEC | O_EXCL | O_NOFOLLOW));
    if (!descriptor.valid())
        return "exclusive-open-failed";
    const auto opened_number = device_number("/proc/self/fd/" + std::to_string(descriptor.get()));
    if (!opened_number.has_value() || *opened_number != device.major_minor)
        return "exclusive-open-identity-mismatch";
    return std::nullopt;
}

void add_mount_reasons(std::vector<std::string>& reasons, const std::vector<std::string>& mount_points) {
    for (const auto& mountpoint : mount_points) {
        add_reason(reasons, "mounted-filesystem:" + mountpoint);
        if (system_mount(mountpoint))
            add_reason(reasons, "system-disk:" + mountpoint);
    }
}

void add_blockers(std::vector<std::string>& reasons, const std::vector<provisioning::SafetyBlocker>& blockers) {
    for (const auto& blocker : blockers)
        add_reason(reasons, blocker.detail.empty() ? blocker.code : blocker.code + ":" + blocker.detail);
}

} // namespace

DestructiveDeviceSafetyInspector::DestructiveDeviceSafetyInspector(
    provisioning::StorageTopologyReader& topology,
    ExclusiveDeviceProbe exclusive_probe
) : topology_(topology),
    exclusive_probe_(exclusive_probe ? std::move(exclusive_probe) : ExclusiveDeviceProbe{probe_exclusive_open}) {
}

std::vector<std::string> DestructiveDeviceSafetyInspector::inspect(
    const ProvisioningDevice& expected_device
) const {
    std::vector<std::string> reasons;
    try {
        const provisioning::StorageTopology topology = topology_.scan();
        const auto selected = std::ranges::find_if(topology.devices, [&](const auto& device) {
            return device.identity.major_minor == expected_device.major_minor;
        });
        if (selected == topology.devices.end()) {
            add_reason(reasons, "block-device-missing");
        } else if (selected->identity.display_path != expected_device.path || selected->identity.sysfs_path != expected_device.sysfs_devpath) {
            add_reason(reasons, "block-device-identity-mismatch");
        } else {
            add_blockers(reasons, selected->blockers);
            add_mount_reasons(reasons, selected->mount_points);
            if (selected->active_swap)
                add_reason(reasons, "active-swap:" + selected->identity.display_path);
            for (const auto& holder : selected->holders)
                add_reason(reasons, "block-holder:" + holder);
            for (const auto& region : selected->regions) {
                const auto* partition = std::get_if<ExistingPartition>(&region);
                if (partition == nullptr)
                    continue;
                add_blockers(reasons, partition->blockers);
                add_mount_reasons(reasons, partition->mount_points);
                if (partition->active_swap)
                    add_reason(reasons, "active-swap:" + partition->identity.display_path);
                for (const auto& holder : partition->holders)
                    add_reason(reasons, "block-holder:" + holder);
            }
        }
    } catch (...) {
        add_reason(reasons, "block-graph-unavailable");
    }
    if (const auto exclusive_reason = exclusive_probe_(expected_device); exclusive_reason.has_value())
        add_reason(reasons, *exclusive_reason);
    return reasons;
}

} // namespace btrfsbackup::daemon::control
