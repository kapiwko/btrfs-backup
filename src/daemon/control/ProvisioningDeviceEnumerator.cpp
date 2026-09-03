// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/ProvisioningDeviceEnumerator.hpp>

#include <algorithm>
#include <ranges>
#include <sstream>
#include <type_traits>

#include <core/Errors.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <provisioning/StorageTopologyReader.hpp>

namespace btrfsbackup::daemon::control {
namespace {

using provisioning::ExistingPartition;
using provisioning::StorageDevice;

bool has_data(const StorageDevice& device) {
    return device.partition_table.type != provisioning::PartitionTableType::None ||
        !device.filesystem.type.empty() ||
        std::ranges::any_of(device.regions, [](const auto& region) {
               return std::holds_alternative<ExistingPartition>(region);
           });
}

bool mounted(const StorageDevice& device) {
    if (!device.mount_points.empty())
        return true;
    return std::ranges::any_of(device.regions, [](const auto& region) {
        const auto* partition = std::get_if<ExistingPartition>(&region);
        return partition != nullptr && !partition->mount_points.empty();
    });
}

std::string graph_fingerprint(const StorageDevice& device) {
    std::ostringstream result;
    result << device.identity.major_minor << '\0' << device.identity.sysfs_path << '\0'
           << device.size_bytes << '\0' << provisioning::partition_table_type_name(device.partition_table.type)
           << '\0' << device.partition_table.identifier << '\0' << device.filesystem.type << '\0'
           << device.filesystem.uuid << '\0';
    for (const auto& holder : device.holders)
        result << "holder:" << holder << '\0';
    for (const auto& mount : device.mount_points)
        result << "mount:" << mount << '\0';
    for (const auto& region : device.regions) {
        std::visit(
            [&](const auto& value) {
                result << value.start_sector << ':' << value.sector_count << ':';
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, ExistingPartition>) {
                    result << value.identity.display_path << ':' << value.identity.major_minor << ':'
                           << value.partition_number << ':' << value.partition_uuid.value_or("") << ':'
                           << value.filesystem.type << ':' << value.filesystem.uuid << ':' << value.active_swap;
                    for (const auto& mount : value.mount_points)
                        result << ":mount=" << mount;
                    for (const auto& holder : value.holders)
                        result << ":holder=" << holder;
                } else {
                    result << value.id;
                }
                result << '\0';
            },
            region
        );
    }
    return result.str();
}

ProvisioningDevice make_provisioning_device(const StorageDevice& device) {
    const std::string serial = !device.identity.serial_short.empty()
        ? device.identity.serial_short
        : device.identity.serial;
    return {
        .path = device.identity.display_path,
        .model = device.display_name,
        .serial = serial,
        .transport = device.transport,
        .size_bytes = device.size_bytes,
        .removable = device.removable,
        .mounted = mounted(device),
        .contains_data = has_data(device),
        .major_minor = device.identity.major_minor,
        .sysfs_devpath = device.identity.sysfs_path,
        .wwn = device.identity.wwn,
        .serial_id = device.identity.serial,
        .serial_short = device.identity.serial_short,
        .device_graph = graph_fingerprint(device),
    };
}

bool same_device_identity(const ProvisioningDevice& expected, const ProvisioningDevice& current) {
    return expected.path == current.path && expected.model == current.model &&
        expected.serial == current.serial && expected.transport == current.transport &&
        expected.size_bytes == current.size_bytes && expected.removable == current.removable &&
        expected.mounted == current.mounted && expected.contains_data == current.contains_data &&
        expected.major_minor == current.major_minor && expected.sysfs_devpath == current.sysfs_devpath &&
        expected.wwn == current.wwn && expected.serial_id == current.serial_id &&
        expected.serial_short == current.serial_short && expected.device_graph == current.device_graph;
}

} // namespace

ProvisioningDevice provisioning_device_snapshot(const provisioning::StorageDevice& device) {
    return make_provisioning_device(device);
}

ProvisioningDeviceEnumerator::ProvisioningDeviceEnumerator(provisioning::StorageTopologyReader& topology)
    : topology_(topology) {
}

std::vector<ProvisioningDevice> ProvisioningDeviceEnumerator::list() {
    const provisioning::StorageTopology topology = topology_.scan();
    std::vector<ProvisioningDevice> result;
    result.reserve(topology.devices.size());
    for (const auto& device : topology.devices)
        result.push_back(make_provisioning_device(device));
    return result;
}

ProvisioningDevice ProvisioningDeviceEnumerator::revalidate(const ProvisioningDevice& expected) {
    const auto current = list();
    const auto selected = std::ranges::find(current, expected.major_minor, &ProvisioningDevice::major_minor);
    if (selected == current.end() || !same_device_identity(expected, *selected))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "selected device identity changed");
    return *selected;
}

} // namespace btrfsbackup::daemon::control
