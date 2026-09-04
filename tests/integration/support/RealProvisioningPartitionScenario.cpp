// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealProvisioningTestEnvironment.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;

void RealProvisioningTestEnvironment::require_existing_partition_preserves_sibling() {
    constexpr std::string_view profile_id = "partition-integration";
    const fs::path profile = fs::path("/etc/btrfs-backup/profiles") / profile_id / "profile.json";
    if (fs::exists(profile))
        throw std::runtime_error("provisioning scenario profile already exists");

    attach_image("512M");
    const auto partitioned = command(
        {"sfdisk", "--quiet", loop_},
        "label: gpt\n"
        "size=128M,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4\n"
        "size=256M,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4\n"
    );
    if (partitioned.status != 0)
        throw std::runtime_error("create provisioning partition table failed: " + command_diagnostic(partitioned));
    require_command({"udevadm", "settle", "--timeout=10"}, "settle provisioning partitions");

    const fs::path first = loop_ + "p1";
    const fs::path second = loop_ + "p2";
    require_block_device(first, "first provisioning partition was not created");
    require_block_device(second, "second provisioning partition was not created");
    require_command({"mkfs.ext4", "-q", "-F", "-L", "PRESERVED", first.string()}, "format sibling");
    require_command({"mkfs.ext4", "-q", "-F", "-L", "REFORMAT", second.string()}, "format target");
    require_command({"mount", first.string(), preserved_mount_.string()}, "mount sibling for setup");
    preserved_mounted_ = true;
    write_test_file(preserved_mount_ / "preserved.txt", "preserved sibling data\n");
    require_command({"sync", "-f", (preserved_mount_ / "preserved.txt").string()}, "sync sibling data");
    require_command({"umount", preserved_mount_.string()}, "unmount sibling after setup");
    preserved_mounted_ = false;

    const auto hash_baseline = command({"sha256sum", first.string()});
    const auto table_baseline = command({"sfdisk", "--dump", loop_});
    if (hash_baseline.status != 0 || table_baseline.status != 0 || hash_baseline.output.empty() ||
        table_baseline.output.empty())
        throw std::runtime_error("cannot capture provisioning preservation baseline");
    const std::string hash_before = hash_baseline.output;
    const std::string table_before = table_baseline.output;
    require_command({"mount", "-o", "ro", first.string(), preserved_mount_.string()}, "mount sibling read-only");
    preserved_mounted_ = true;

    start_manager();
    try {
        const auto response = nlohmann::json::parse(
            provision(second, "reformat-existing-partition", profile_id)
        );
        if (response.at("state") != "succeeded" || !fs::is_regular_file(profile))
            throw std::runtime_error("manager did not publish successful partition provisioning");
        if (command({"findmnt", "--mountpoint", preserved_mount_.string()}).status != 0)
            throw std::runtime_error("partition provisioning unmounted the sibling partition");
        std::ifstream content(preserved_mount_ / "preserved.txt");
        const std::string preserved{std::istreambuf_iterator<char>(content), std::istreambuf_iterator<char>()};
        if (preserved != "preserved sibling data\n")
            throw std::runtime_error("partition provisioning changed mounted sibling data");
        delete_profile(profile_id);
        stop_manager();
    } catch (...) {
        if (manager_started_ && fs::exists(profile)) {
            try {
                delete_profile(profile_id);
            } catch (...) {}
        }
        throw;
    }

    require_command({"umount", preserved_mount_.string()}, "unmount verified sibling");
    preserved_mounted_ = false;
    const auto hash_after = command({"sha256sum", first.string()});
    const auto table_after = command({"sfdisk", "--dump", loop_});
    if (hash_after.status != 0 || hash_after.output != hash_before)
        throw std::runtime_error("partition provisioning changed sibling bytes");
    if (table_after.status != 0 || table_after.output != table_before)
        throw std::runtime_error("partition provisioning changed its parent partition table");
    const auto sibling_type = command({"blkid", "-s", "TYPE", "-o", "value", first.string()});
    if (sibling_type.status != 0 || trim_output(sibling_type.output) != "ext4")
        throw std::runtime_error("partition provisioning changed the sibling filesystem");
    if (command({"cryptsetup", "isLuks", "--type", "luks2", second.string()}).status != 0)
        throw std::runtime_error("selected partition was not formatted as LUKS2");

    require_command({"losetup", "-d", loop_}, "detach verified provisioning loop");
    loop_.clear();
}

void RealProvisioningTestEnvironment::require_lvm_member_is_rejected_without_writes() {
    constexpr std::string_view profile_id = "lvm-rejection-integration";
    const fs::path profile = fs::path("/etc/btrfs-backup/profiles") / profile_id / "profile.json";
    attach_image("256M");
    const auto partitioned = command(
        {"sfdisk", "--quiet", loop_},
        "label: gpt\nsize=192M,type=E6D6D379-F507-44C2-A23C-238F2A3DF928\n"
    );
    if (partitioned.status != 0)
        throw std::runtime_error("create LVM rejection partition failed: " + command_diagnostic(partitioned));
    require_command({"udevadm", "settle", "--timeout=10"}, "settle LVM rejection partition");
    const fs::path partition = loop_ + "p1";
    require_block_device(partition, "LVM rejection partition was not created");
    require_command({"pvcreate", "--yes", "--force", partition.string()}, "create disposable LVM member");
    require_command({"udevadm", "settle", "--timeout=10"}, "settle LVM member signature");
    const auto hash_before = command({"sha256sum", partition.string()});
    if (hash_before.status != 0)
        throw std::runtime_error("cannot capture LVM member baseline");

    start_manager();
    const auto rejected = command(
        {client_.string(), partition.string(), source_.string(), "-", "reformat-existing-partition", std::string(profile_id)},
        passphrase_
    );
    stop_manager();
    if (rejected.status == 0 || fs::exists(profile))
        throw std::runtime_error("LVM member was accepted for destructive preparation");
    const auto hash_after = command({"sha256sum", partition.string()});
    if (hash_after.status != 0 || hash_after.output != hash_before.output)
        throw std::runtime_error("rejected LVM member was modified");

    require_command({"pvremove", "--yes", "--force", partition.string()}, "remove disposable LVM member");
    require_command({"losetup", "-d", loop_}, "detach LVM rejection loop");
    loop_.clear();
}

} // namespace btrfsbackup::integration
