// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/ProfileWizardDevice.hpp>

#include <libudev.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <ostream>
#include <string>

#include <core/Errors.hpp>
#include <config/wizard/ProfileWizardPrompt.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux {

namespace {

std::string c_string(const char* value) {
    return value == nullptr ? "" : value;
}

std::string udev_property(udev_device* device, const char* key) {
    return c_string(udev_device_get_property_value(device, key));
}

std::string udev_sysattr(udev_device* device, const char* key) {
    return c_string(udev_device_get_sysattr_value(device, key));
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

std::vector<DeviceCandidate> detect_luks_devices() {
    std::unique_ptr<udev, decltype(&udev_unref)> udev_context(udev_new(), udev_unref);
    if (!udev_context) {
        throw ValidationError("could not initialize libudev");
    }
    std::unique_ptr<udev_enumerate, decltype(&udev_enumerate_unref)> enumerate(
        udev_enumerate_new(udev_context.get()),
        udev_enumerate_unref
    );
    if (!enumerate) {
        throw ValidationError("could not enumerate block devices");
    }
    udev_enumerate_add_match_subsystem(enumerate.get(), "block");
    udev_enumerate_scan_devices(enumerate.get());

    std::vector<DeviceCandidate> devices;
    udev_list_entry* entries = udev_enumerate_get_list_entry(enumerate.get());
    udev_list_entry* entry = nullptr;
    udev_list_entry_foreach(entry, entries) {
        const char* syspath = udev_list_entry_get_name(entry);
        std::unique_ptr<udev_device, decltype(&udev_device_unref)> device(
            udev_device_new_from_syspath(udev_context.get(), syspath),
            udev_device_unref
        );
        if (!device) {
            continue;
        }
        if (udev_property(device.get(), "ID_FS_TYPE") != "crypto_LUKS") {
            continue;
        }
        std::string devtype = udev_property(device.get(), "DEVTYPE");
        if (devtype != "partition" && devtype != "disk") {
            continue;
        }
        std::string uuid = udev_property(device.get(), "ID_FS_UUID");
        if (uuid.empty()) {
            continue;
        }
        std::string devnode = c_string(udev_device_get_devnode(device.get()));
        if (devnode.empty()) {
            continue;
        }
        std::string transport = udev_property(device.get(), "ID_BUS");
        devices.push_back({devnode, devtype == "partition" ? "part" : "disk", "", transport, udev_property(device.get(), "ID_MODEL"), udev_property(device.get(), "ID_SERIAL_SHORT"), lower(uuid), btrfsbackup::config::wizard::trim_text(udev_sysattr(device.get(), "removable")) == "1" || transport == "usb"});
    }
    std::sort(devices.begin(), devices.end(), [](const DeviceCandidate& left, const DeviceCandidate& right) {
        return left.path < right.path;
    });
    return devices;
}

std::string best_device_reference(const DeviceCandidate& device) {
    fs::path uuid_path = fs::path("/dev/disk/by-uuid") / device.uuid;
    if (fs::exists(uuid_path)) {
        return uuid_path.string();
    }
    return device.path;
}

std::string udev_property_for_device(const std::string& device_path, const char* key) {
    struct stat stat_buffer{};
    if (stat(device_path.c_str(), &stat_buffer) != 0) {
        return "";
    }
    std::unique_ptr<udev, decltype(&udev_unref)> udev_context(udev_new(), udev_unref);
    if (!udev_context) {
        return "";
    }
    std::unique_ptr<udev_device, decltype(&udev_device_unref)> device(
        udev_device_new_from_devnum(udev_context.get(), 'b', stat_buffer.st_rdev),
        udev_device_unref
    );
    if (!device) {
        return "";
    }
    return udev_property(device.get(), key);
}

std::string detect_btrfs_uuid(const std::string& mapper_name) {
    fs::path mapper_path = fs::path("/dev/mapper") / mapper_name;
    if (!fs::exists(mapper_path)) {
        return "";
    }
    return udev_property_for_device(mapper_path.string(), "ID_FS_UUID");
}

DeviceCandidate select_device(std::istream& input, std::ostream& output) {
    std::vector<DeviceCandidate> devices = detect_luks_devices();
    if (devices.empty()) {
        throw ValidationError("no LUKS block devices were detected");
    }
    output << "Detected LUKS devices:\n";
    for (std::size_t i = 0; i < devices.size(); ++i) {
        output << " " << (i + 1) << ") "
               << (devices[i].removable ? "[removable] " : "")
               << devices[i].path << " | " << (devices[i].size.empty() ? "?" : devices[i].size)
               << " | " << (devices[i].transport.empty() ? "?" : devices[i].transport)
               << " | " << (devices[i].model.empty() ? "?" : devices[i].model)
               << " | UUID=" << devices[i].uuid
               << " | " << (devices[i].serial.empty() ? "no-serial" : devices[i].serial)
               << '\n';
    }
    std::string choice = btrfsbackup::config::wizard::prompt_value(input, output, "Select backup device", "1");
    if (!std::all_of(choice.begin(), choice.end(), [](unsigned char c) { return std::isdigit(c); })) {
        throw ValidationError("invalid device selection: " + choice);
    }
    std::size_t index = static_cast<std::size_t>(std::stoul(choice));
    if (index == 0 || index > devices.size()) {
        throw ValidationError("device selection out of range: " + choice);
    }
    return devices[index - 1];
}

} // namespace btrfsbackup::platform::linux
