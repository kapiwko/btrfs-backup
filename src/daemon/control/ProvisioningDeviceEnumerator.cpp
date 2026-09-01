// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/ProvisioningDeviceEnumerator.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <ranges>
#include <sstream>

#include <backup/ports/ICommandRunner.hpp>
#include <config/json/Json.hpp>
#include <core/Errors.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

using config::json::Json;

std::string json_string(const Json& object, const char* key) {
    const auto value = object.find(key);
    return value != object.end() && value->is_string() ? value->get<std::string>() : std::string{};
}

std::uint64_t json_size(const Json& object, const char* key) {
    const auto value = object.find(key);
    if (value == object.end())
        return 0;
    if (value->is_number_unsigned())
        return value->get<std::uint64_t>();
    if (value->is_number_integer()) {
        const auto number = value->get<std::int64_t>();
        return number > 0 ? static_cast<std::uint64_t>(number) : 0;
    }
    return 0;
}

bool json_boolean(const Json& object, const char* key) {
    const auto value = object.find(key);
    if (value == object.end())
        return false;
    if (value->is_boolean())
        return value->get<bool>();
    return value->is_number_integer() && value->get<int>() != 0;
}

bool nonempty_mounts(const Json& node) {
    if (node.contains("mountpoints") && node.at("mountpoints").is_array()) {
        for (const auto& mount : node.at("mountpoints"))
            if (mount.is_string() && !mount.get<std::string>().empty())
                return true;
    }
    return node.contains("children") && node.at("children").is_array() &&
        std::ranges::any_of(node.at("children"), nonempty_mounts);
}

bool contains_data(const Json& node) {
    if (!json_string(node, "fstype").empty() || !json_string(node, "pttype").empty())
        return true;
    return node.contains("children") && node.at("children").is_array() && !node.at("children").empty();
}

Json device_graph_node(const Json& node) {
    Json result{
        {"path", json_string(node, "path")},
        {"type", json_string(node, "type")},
        {"majorMinor", json_string(node, "maj:min")},
        {"kernelName", json_string(node, "kname")},
        {"parentKernelName", json_string(node, "pkname")},
        {"sizeBytes", json_size(node, "size")},
        {"filesystemType", json_string(node, "fstype")},
        {"partitionTableType", json_string(node, "pttype")},
    };
    result["children"] = Json::array();
    if (node.contains("children") && node.at("children").is_array())
        for (const auto& child : node.at("children"))
            result["children"].push_back(device_graph_node(child));
    return result;
}

std::map<std::string, std::string> parse_udev_properties(const std::string& payload) {
    std::map<std::string, std::string> result;
    std::istringstream lines(payload);
    std::string line;
    while (std::getline(lines, line)) {
        const auto separator = line.find('=');
        if (separator != std::string::npos && separator != 0)
            result.insert_or_assign(line.substr(0, separator), line.substr(separator + 1));
    }
    return result;
}

std::string property(const std::map<std::string, std::string>& properties, const char* key) {
    const auto value = properties.find(key);
    return value == properties.end() ? std::string{} : value->second;
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

std::vector<ProvisioningDevice> parse_devices(const std::string& payload) {
    const Json document = Json::parse(payload);
    if (!document.is_object() || !document.contains("blockdevices") || !document.at("blockdevices").is_array())
        throw ValidationError("lsblk returned invalid device data");
    std::vector<ProvisioningDevice> result;
    for (const Json& node : document.at("blockdevices")) {
        if (!node.is_object() || json_string(node, "type") != "disk")
            continue;
        const std::string path = json_string(node, "path");
        const std::uint64_t size = json_size(node, "size");
        if (path.empty() || !fs::path(path).is_absolute() || size == 0)
            continue;
        result.push_back({
            .candidate_id = {},
            .path = path,
            .model = json_string(node, "model"),
            .serial = json_string(node, "serial"),
            .transport = json_string(node, "tran"),
            .size_bytes = size,
            .removable = json_boolean(node, "rm"),
            .mounted = nonempty_mounts(node),
            .contains_data = contains_data(node),
            .major_minor = json_string(node, "maj:min"),
            .sysfs_devpath = {},
            .wwn = json_string(node, "wwn"),
            .serial_id = {},
            .serial_short = {},
            .device_graph = device_graph_node(node).dump(),
        });
    }
    return result;
}

} // namespace

ProvisioningDeviceEnumerator::ProvisioningDeviceEnumerator(backup::ICommandRunner& commands)
    : commands_(commands) {
}

std::vector<ProvisioningDevice> ProvisioningDeviceEnumerator::list() {
    std::vector<ProvisioningDevice> result = parse_devices(backup::capture_command(commands_, {"lsblk", "--json", "--tree", "--bytes", "--paths", "--output", "PATH,TYPE,SIZE,MODEL,SERIAL,WWN,TRAN,RM,FSTYPE,PTTYPE,MOUNTPOINTS,MAJ:MIN,KNAME,PKNAME"}));
    for (auto& device : result) {
        const auto properties = parse_udev_properties(backup::capture_command(commands_, {"udevadm", "info", "--query=property", "--name", device.path}));
        const std::string udev_major = property(properties, "MAJOR");
        const std::string udev_minor = property(properties, "MINOR");
        const std::string udev_major_minor = udev_major.empty() || udev_minor.empty()
            ? std::string{}
            : udev_major + ":" + udev_minor;
        if (!udev_major_minor.empty() && device.major_minor != udev_major_minor)
            device.major_minor.clear();
        device.sysfs_devpath = property(properties, "DEVPATH");
        device.serial_id = property(properties, "ID_SERIAL");
        device.serial_short = property(properties, "ID_SERIAL_SHORT");
        const std::string udev_wwn = property(properties, "ID_WWN_WITH_EXTENSION").empty()
            ? property(properties, "ID_WWN")
            : property(properties, "ID_WWN_WITH_EXTENSION");
        if (!udev_wwn.empty())
            device.wwn = udev_wwn;
        const std::string udev_transport = property(properties, "ID_BUS");
        if (!udev_transport.empty())
            device.transport = udev_transport;
        if (!device.serial_short.empty())
            device.serial = device.serial_short;
        else if (!device.serial_id.empty())
            device.serial = device.serial_id;
    }
    std::erase_if(result, [](const auto& device) {
        return device.major_minor.empty() || device.sysfs_devpath.empty() || device.transport.empty() ||
            device.device_graph.empty() ||
            (device.wwn.empty() && device.serial_id.empty() && device.serial_short.empty());
    });
    return result;
}

ProvisioningDevice ProvisioningDeviceEnumerator::revalidate(const ProvisioningDevice& expected) {
    const auto current = list();
    const auto selected = std::ranges::find(current, expected.path, &ProvisioningDevice::path);
    if (selected == current.end() || !same_device_identity(expected, *selected))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "selected device identity changed");
    return *selected;
}

} // namespace btrfsbackup::daemon::control
