// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup::wizard {

struct DeviceCandidate {
    std::string path;
    std::string type;
    std::string size;
    std::string transport;
    std::string model;
    std::string serial;
    std::string uuid;
    bool removable = false;
};

std::vector<DeviceCandidate> detect_luks_devices();
std::string best_device_reference(const DeviceCandidate& device);
std::string udev_property_for_device(const std::string& device_path, const char* key);
std::string detect_btrfs_uuid(const std::string& mapper_name);
DeviceCandidate select_device(std::istream& input, std::ostream& output);

} // namespace btrfsbackup::wizard
