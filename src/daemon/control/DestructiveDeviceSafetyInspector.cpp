// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DestructiveDeviceSafetyInspector.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

#include <backup/ports/ICommandRunner.hpp>
#include <config/json/Json.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

using config::json::Json;
using platform::linux::OwnedFileDescriptor;

std::string json_string(const Json& object, const char* key) {
    const auto value = object.find(key);
    return value != object.end() && value->is_string() ? value->get<std::string>() : std::string{};
}

void add_reason(std::vector<std::string>& reasons, std::string reason) {
    if (std::ranges::find(reasons, reason) == reasons.end())
        reasons.push_back(std::move(reason));
}

bool system_mount(const std::string& mountpoint) {
    static const std::set<std::string> system_mounts{"/", "/boot", "/boot/efi", "/home", "/usr", "/var"};
    return system_mounts.contains(mountpoint);
}

const Json* find_device(const Json& nodes, const std::string& major_minor, bool& nested, bool child = false) {
    if (!nodes.is_array())
        return nullptr;
    for (const auto& node : nodes) {
        if (!node.is_object())
            continue;
        if (json_string(node, "maj:min") == major_minor) {
            nested = child;
            return &node;
        }
        if (node.contains("children"))
            if (const Json* found = find_device(node.at("children"), major_minor, nested, true))
                return found;
    }
    return nullptr;
}

void inspect_subtree(
    const Json& node,
    bool root,
    std::set<std::string>& paths,
    std::set<std::string>& device_numbers,
    std::vector<std::string>& reasons
) {
    const std::string path = json_string(node, "path");
    const std::string device_number = json_string(node, "maj:min");
    const std::string type = json_string(node, "type");
    if (!path.empty())
        paths.insert(path);
    if (!device_number.empty())
        device_numbers.insert(device_number);
    if (!root && !type.empty() && type != "part")
        add_reason(reasons, "active-block-layer:" + type);
    const auto mountpoints = node.find("mountpoints");
    if (mountpoints != node.end() && mountpoints->is_array()) {
        for (const auto& value : *mountpoints) {
            if (!value.is_string() || value.get<std::string>().empty())
                continue;
            const std::string mountpoint = value.get<std::string>();
            add_reason(reasons, "mounted-filesystem:" + mountpoint);
            if (system_mount(mountpoint))
                add_reason(reasons, "system-disk:" + mountpoint);
        }
    }
    if (node.contains("children") && node.at("children").is_array())
        for (const auto& child : node.at("children"))
            inspect_subtree(child, false, paths, device_numbers, reasons);
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

} // namespace

DestructiveDeviceSafetyInspector::DestructiveDeviceSafetyInspector(
    backup::ICommandRunner& commands,
    fs::path proc_swaps,
    fs::path sys_dev_block,
    ExclusiveDeviceProbe exclusive_probe
) : commands_(commands), proc_swaps_(std::move(proc_swaps)), sys_dev_block_(std::move(sys_dev_block)),
    exclusive_probe_(exclusive_probe ? std::move(exclusive_probe) : ExclusiveDeviceProbe{probe_exclusive_open}) {
}

std::vector<std::string> DestructiveDeviceSafetyInspector::inspect(
    const ProvisioningDevice& expected_device
) const {
    std::vector<std::string> reasons;
    std::set<std::string> paths{expected_device.path};
    std::set<std::string> device_numbers{expected_device.major_minor};
    try {
        const Json graph = Json::parse(backup::capture_command(commands_, {"lsblk", "--json", "--tree", "--paths", "--output", "PATH,TYPE,MAJ:MIN,PKNAME,MOUNTPOINTS"}));
        if (!graph.is_object() || !graph.contains("blockdevices")) {
            add_reason(reasons, "block-graph-unavailable");
        } else {
            bool nested = false;
            const Json* selected = find_device(graph.at("blockdevices"), expected_device.major_minor, nested);
            if (selected == nullptr) {
                add_reason(reasons, "block-device-missing");
            } else {
                if (nested)
                    add_reason(reasons, "dependent-block-parent");
                inspect_subtree(*selected, true, paths, device_numbers, reasons);
            }
        }
    } catch (...) {
        add_reason(reasons, "block-graph-unavailable");
    }

    std::ifstream swaps(proc_swaps_);
    if (!swaps) {
        add_reason(reasons, "swap-state-unavailable");
    } else {
        std::string line;
        static_cast<void>(std::getline(swaps, line));
        while (std::getline(swaps, line)) {
            std::istringstream fields(line);
            std::string swap_path;
            fields >> swap_path;
            const auto swap_number = device_number(swap_path);
            if (paths.contains(swap_path) ||
                (swap_number.has_value() && device_numbers.contains(*swap_number)))
                add_reason(reasons, "active-swap:" + swap_path);
        }
    }

    for (const auto& number : device_numbers) {
        std::error_code error;
        const fs::path holders = sys_dev_block_ / number / "holders";
        if (!fs::is_directory(holders, error) || error) {
            add_reason(reasons, "holder-state-unavailable:" + number);
            continue;
        }
        for (fs::directory_iterator item(holders, error); !error && item != fs::directory_iterator{}; item.increment(error))
            add_reason(reasons, "block-holder:" + item->path().filename().string());
        if (error)
            add_reason(reasons, "holder-state-unavailable:" + number);
    }

    if (const auto exclusive_reason = exclusive_probe_(expected_device); exclusive_reason.has_value())
        add_reason(reasons, *exclusive_reason);
    return reasons;
}

} // namespace btrfsbackup::daemon::control
