// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealProvisioningTestEnvironment.hpp"

#include <stdexcept>

#include <nlohmann/json.hpp>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;
using Json = nlohmann::json;

void RealProvisioningTestEnvironment::require_whole_device_is_replaced() {
    constexpr std::string_view profile_id = "whole-device-integration";
    const fs::path profile = fs::path("/etc/btrfs-backup/profiles") / profile_id / "profile.json";
    if (fs::exists(profile))
        throw std::runtime_error("whole-device provisioning profile already exists");

    attach_image("640M");
    require_command({"mkfs.ext4", "-q", "-F", "-L", "ERASE-WHOLE", loop_}, "format disposable whole device");
    require_command({"udevadm", "settle", "--timeout=10"}, "settle whole device");
    start_manager();
    try {
        const Json response = Json::parse(provision(loop_, "erase-whole-device", profile_id));
        if (response.at("state") != "succeeded" || !fs::is_regular_file(profile))
            throw std::runtime_error("manager did not publish successful whole-device provisioning");
        delete_profile(profile_id);
        stop_manager();
    } catch (...) {
        if (manager_started_ && fs::exists(profile)) {
            try { delete_profile(profile_id); } catch (...) {}
        }
        throw;
    }

    require_command({"udevadm", "settle", "--timeout=10"}, "settle whole-device partition");
    const fs::path backup = loop_ + "p1";
    require_block_device(backup, "whole-device backup partition was not created");
    const auto table = command({"sfdisk", "--json", loop_});
    if (table.status != 0 || Json::parse(table.output).at("partitiontable").at("label") != "gpt")
        throw std::runtime_error("whole-device provisioning did not create GPT");
    if (command({"cryptsetup", "isLuks", "--type", "luks2", backup.string()}).status != 0)
        throw std::runtime_error("whole-device provisioning did not create its LUKS2 partition");

    require_command({"losetup", "-d", loop_}, "detach whole-device provisioning loop");
    loop_.clear();
}

} // namespace btrfsbackup::integration
