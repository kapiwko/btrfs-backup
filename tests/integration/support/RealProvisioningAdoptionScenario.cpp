// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealProvisioningTestEnvironment.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;
using Json = nlohmann::json;

void RealProvisioningTestEnvironment::require_existing_target_is_adopted() {
    constexpr std::string_view profile_id = "adoption-integration";
    const fs::path profile = fs::path("/etc/btrfs-backup/profiles") / profile_id / "profile.json";
    if (fs::exists(profile))
        throw std::runtime_error("adoption provisioning profile already exists");

    attach_image("512M");
    const auto partitioned = command(
        {"sfdisk", "--quiet", loop_},
        "label: gpt\nsize=448M,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4\n"
    );
    if (partitioned.status != 0)
        throw std::runtime_error("create adoption partition failed: " + command_diagnostic(partitioned));
    require_command({"udevadm", "settle", "--timeout=10"}, "settle adoption partition");
    const fs::path partition = loop_ + "p1";
    require_block_device(partition, "adoption partition node was not created");

    const auto formatted = command(
        {"cryptsetup", "luksFormat", "--batch-mode", "--type", "luks2", "--pbkdf", "pbkdf2",
         "--pbkdf-force-iterations", "1000", "--key-file", "-", partition.string()},
        passphrase_
    );
    if (formatted.status != 0)
        throw std::runtime_error("format adoption LUKS2 failed: " + command_diagnostic(formatted));
    const auto opened = command(
        {"cryptsetup", "open", "--key-file", "-", partition.string(), adoption_mapper_name_},
        passphrase_
    );
    if (opened.status != 0)
        throw std::runtime_error("open adoption mapper failed: " + command_diagnostic(opened));
    adoption_mapper_open_ = true;
    require_command({"udevadm", "settle", "--timeout=10"}, "settle adoption mapper");
    require_command({"dmsetup", "mknodes", adoption_mapper_name_}, "materialize adoption mapper");
    require_block_device(adoption_mapper_path_, "adoption mapper node was not created");
    require_command({"mkfs.btrfs", "-q", "-f", adoption_mapper_path_.string()}, "format adoption Btrfs");
    require_command({"mount", adoption_mapper_path_.string(), staging_mount_.string()}, "mount adoption staging");
    staging_mounted_ = true;

    const auto btrfs_identity = command({"findmnt", "-n", "-o", "UUID", "-M", staging_mount_.string()});
    const auto luks_identity = command({"cryptsetup", "luksUUID", partition.string()});
    const std::string btrfs_uuid = trim_output(btrfs_identity.output);
    const std::string luks_uuid = trim_output(luks_identity.output);
    if (btrfs_identity.status != 0 || luks_identity.status != 0 || btrfs_uuid.empty() || luks_uuid.empty())
        throw std::runtime_error("cannot read adoption target identities");
    const std::string repository_id = "adoption-" + btrfs_uuid;
    write_test_file(
        staging_mount_ / "repository.json",
        Json{{"schemaVersion", 1}, {"repositoryId", repository_id}, {"targetFilesystemUuid", btrfs_uuid},
             {"createdAt", "2026-08-25T08:00:00Z"}, {"features", Json::array({"catalog-v1"})}}.dump() + "\n"
    );
    write_test_file(
        staging_mount_ / "catalog.json",
        Json{{"schemaVersion", 1}, {"generation", 1}, {"snapshots", Json::array()}}.dump() + "\n"
    );
    require_command({"sync", "-f", (staging_mount_ / "repository.json").string()}, "sync repository metadata");
    require_command({"sync", "-f", (staging_mount_ / "catalog.json").string()}, "sync catalog metadata");
    require_command({"umount", staging_mount_.string()}, "unmount adoption staging");
    staging_mounted_ = false;
    require_command({"cryptsetup", "close", adoption_mapper_name_}, "close adoption mapper");
    adoption_mapper_open_ = false;
    const auto hash_before = command({"sha256sum", partition.string()});
    if (hash_before.status != 0)
        throw std::runtime_error("cannot capture adoption partition baseline");

    start_manager();
    try {
        const std::string payload = provision(partition, "adopt-existing-target", profile_id);
        const Json response = Json::parse(payload);
        if (response.at("state") != "succeeded" || !fs::is_regular_file(profile))
            throw std::runtime_error("manager did not publish successful target adoption");
        if (payload.contains(repository_id))
            throw std::runtime_error("adoption status leaked the repository identifier");
        std::ifstream profile_input(profile);
        const std::string profile_document{
            std::istreambuf_iterator<char>(profile_input), std::istreambuf_iterator<char>()
        };
        if (!profile_document.contains(luks_uuid) || !profile_document.contains(btrfs_uuid))
            throw std::runtime_error("adopted profile omitted inspected target identities");
        delete_profile(profile_id);
        stop_manager();
    } catch (...) {
        if (manager_started_ && fs::exists(profile)) {
            try { delete_profile(profile_id); } catch (...) {}
        }
        throw;
    }

    const auto hash_after = command({"sha256sum", partition.string()});
    const auto current_luks_identity = command({"cryptsetup", "luksUUID", partition.string()});
    if (hash_after.status != 0 || hash_after.output != hash_before.output)
        throw std::runtime_error("existing-target adoption modified the selected partition");
    if (current_luks_identity.status != 0 || trim_output(current_luks_identity.output) != luks_uuid)
        throw std::runtime_error("existing-target adoption changed the LUKS identity");

    require_command({"losetup", "-d", loop_}, "detach adoption provisioning loop");
    loop_.clear();
}

} // namespace btrfsbackup::integration
