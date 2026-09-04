// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealProvisioningTestEnvironment.hpp"

#include <stdexcept>

#include <nlohmann/json.hpp>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

[[nodiscard]] Json partition_geometry(const Json& table, const fs::path& partition) {
    for (const auto& entry : table.at("partitiontable").at("partitions"))
        if (entry.at("node").get<std::string>() == partition)
            return Json{{"start", entry.at("start")}, {"size", entry.at("size")}, {"type", entry.at("type")}};
    throw std::runtime_error("partition table omitted the preserved partition");
}

} // namespace

void RealProvisioningTestEnvironment::require_unallocated_space_preserves_partition() {
    constexpr std::string_view profile_id = "free-space-integration";
    const fs::path profile = fs::path("/etc/btrfs-backup/profiles") / profile_id / "profile.json";
    if (fs::exists(profile))
        throw std::runtime_error("free-space provisioning profile already exists");

    attach_image("768M");
    const auto partitioned = command(
        {"sfdisk", "--quiet", loop_},
        "label: gpt\nsize=256M,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4\n"
    );
    if (partitioned.status != 0)
        throw std::runtime_error("create free-space partition table failed: " + command_diagnostic(partitioned));
    require_command({"udevadm", "settle", "--timeout=10"}, "settle free-space partitions");
    const fs::path preserved = loop_ + "p1";
    const fs::path backup = loop_ + "p2";
    if (!fs::is_block_file(preserved))
        throw std::runtime_error("preserved partition node was not created");
    require_command({"mkfs.ext4", "-q", "-F", "-L", "PRESERVED", preserved.string()}, "format preserved partition");
    const auto hash_before = command({"sha256sum", preserved.string()});
    const auto table_before = command({"sfdisk", "--json", loop_});
    if (hash_before.status != 0 || table_before.status != 0)
        throw std::runtime_error("cannot capture free-space preservation baseline");
    const Json geometry_before = partition_geometry(Json::parse(table_before.output), preserved);

    start_manager();
    try {
        const Json response = Json::parse(provision(loop_, "create-partition-in-unallocated-space", profile_id));
        if (response.at("state") != "succeeded" || !fs::is_regular_file(profile))
            throw std::runtime_error("manager did not publish successful free-space provisioning");
        delete_profile(profile_id);
        stop_manager();
    } catch (...) {
        if (manager_started_ && fs::exists(profile)) {
            try { delete_profile(profile_id); } catch (...) {}
        }
        throw;
    }

    require_command({"udevadm", "settle", "--timeout=10"}, "settle created backup partition");
    const auto hash_after = command({"sha256sum", preserved.string()});
    const auto table_after = command({"sfdisk", "--json", loop_});
    if (hash_after.status != 0 || hash_after.output != hash_before.output)
        throw std::runtime_error("free-space provisioning changed preserved partition bytes");
    if (table_after.status != 0 || partition_geometry(Json::parse(table_after.output), preserved) != geometry_before)
        throw std::runtime_error("free-space provisioning changed preserved partition geometry");
    if (!fs::is_block_file(backup) ||
        command({"cryptsetup", "isLuks", "--type", "luks2", backup.string()}).status != 0)
        throw std::runtime_error("free-space provisioning did not create a LUKS2 backup partition");
    const auto preserved_type = command({"blkid", "-s", "TYPE", "-o", "value", preserved.string()});
    if (preserved_type.status != 0 || trim_output(preserved_type.output) != "ext4")
        throw std::runtime_error("free-space provisioning changed the existing filesystem");

    require_command({"losetup", "-d", loop_}, "detach free-space provisioning loop");
    loop_.clear();
}

} // namespace btrfsbackup::integration
