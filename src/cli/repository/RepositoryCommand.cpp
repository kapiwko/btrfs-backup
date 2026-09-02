// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/repository/RepositoryCommand.hpp>

#include <sys/random.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <print>
#include <set>
#include <sstream>

#include <backup/model/SnapshotInventory.hpp>
#include <backup/planning/BackupPreflightValidation.hpp>
#include <config/json/JsonIo.hpp>
#include <core/Errors.hpp>
#include <core/RuntimeTime.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>
#include <platform/linux/filesystem/FileIo.hpp>
#include <platform/linux/filesystem/FileLock.hpp>
#include <platform/linux/storage/LibBtrfsOperations.hpp>
#include <platform/linux/storage/MountInfo.hpp>
#include <restore/RepositoryDiscoveryService.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::cli::repository {
namespace {

struct Options {
    std::string profile_id = "default";
    bool apply = false;
};

Options parse(const std::vector<std::string>& args) {
    if (args.empty() || args.front() == "-h" || args.front() == "--help") {
        std::println("Usage: btrfs-backupctl repository rebuild [--profile ID] [--apply]");
        return {};
    }
    if (args.front() != "rebuild")
        throw ValidationError("unknown repository command: " + args.front());
    Options options;
    for (std::size_t index = 1; index < args.size(); ++index) {
        if (args[index] == "--profile") {
            if (++index >= args.size())
                throw ValidationError("--profile requires a value");
            options.profile_id = args[index];
        } else if (args[index] == "--apply") {
            options.apply = true;
        } else {
            throw ValidationError("unknown repository option: " + args[index]);
        }
    }
    return options;
}

std::string random_id() {
    std::array<unsigned char, 16> bytes{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            throw ValidationError("cannot generate repository identifier");
        }
    }
    std::ostringstream result;
    result << "repository-" << std::hex << std::setfill('0');
    for (const auto byte : bytes)
        result << std::setw(2) << static_cast<unsigned>(byte);
    return result.str();
}

std::string host_id() {
    std::ifstream input("/etc/machine-id");
    std::string value;
    input >> value;
    if (value.empty())
        throw ValidationError("cannot read /etc/machine-id");
    return value;
}

} // namespace

int repository(const fs::path& config_root, const std::vector<std::string>& args, std::ostream& output) {
    if (args.empty() || args.front() == "-h" || args.front() == "--help") {
        (void)parse(args);
        return 0;
    }
    if (geteuid() != 0)
        throw ValidationError("repository rebuild requires root privileges");
    const Options options = parse(args);
    platform::linux::config::FileProfileRepository profiles(config_root);
    const auto profile = profiles.get(ProfileId{options.profile_id}).profile;

    platform::linux::filesystem::FileLock profile_lock(
        platform::linux::filesystem::profile_lock_path(platform::linux::filesystem::default_lock_root(), profile.id)
    );
    platform::linux::filesystem::FileLock target_lock(
        platform::linux::filesystem::target_lock_path(platform::linux::filesystem::default_lock_root(), profile.target.luks_uuid)
    );
    if (!profile_lock.try_acquire() || !target_lock.try_acquire())
        throw ValidationError("profile or backup target is currently in use");

    platform::linux::storage::LinuxMountInspector mounts;
    const auto mount = backup::planning::validate_backup_target_mount(profile, mounts.inspect());
    const fs::path root = profile.paths.remote_root.value();
    const fs::path repository_file = root / "repository.json";
    const fs::path catalog_file = root / "catalog.json";
    const bool repository_exists = fs::exists(repository_file);
    const bool catalog_exists = fs::exists(catalog_file);
    if (repository_exists != catalog_exists)
        throw ValidationError("repository metadata is incomplete; refusing to replace only one metadata file");

    std::string repository_id;
    std::string created_at;
    std::uint64_t generation = 1;
    if (repository_exists) {
        restore::RepositoryDiscoveryService discovery([](const fs::path& path) {
            const auto value = platform::linux::storage::read_btrfs_snapshot_metadata(path);
            if (!value)
                return std::optional<restore::DiscoveredSnapshotMetadata>{};
            return std::optional{restore::DiscoveredSnapshotMetadata{
                value->is_subvolume,
                value->readonly,
                value->uuid.value(),
                value->received_uuid.value()
            }};
        });
        const auto current = discovery.discover(root);
        if (current.identity().target_filesystem_uuid != mount.filesystem_uuid)
            throw ValidationError("repository filesystem UUID does not match the mounted target");
        repository_id = current.identity().repository_id;
        created_at = format_utc_iso_timestamp(current.identity().created_at);
        generation = current.generation() + 1;
    } else {
        repository_id = random_id();
        created_at = format_utc_iso_timestamp(std::chrono::system_clock::now());
    }

    config::json::Json snapshots = config::json::Json::array();
    std::set<std::string> snapshot_ids;
    const std::string catalog_host_id = host_id();
    for (const auto& source : profile.sources) {
        if (!source.enabled)
            continue;
        const fs::path relative_root = source.remote_subdir.value();
        const fs::path source_root = root / relative_root;
        const auto inventory = backup::list_snapshot_inventory(
            source_root,
            source.id,
            backup::SnapshotSide::Remote,
            platform::linux::storage::read_btrfs_snapshot_metadata
        );
        for (const auto& snapshot : inventory) {
            if (!snapshot.readonly)
                throw ValidationError("remote snapshot is not read-only: " + snapshot.path.string());
            if (!snapshot_ids.insert(snapshot.name).second)
                throw ValidationError("duplicate snapshot identifier: " + snapshot.name);
            config::json::Json entry = {
                {"snapshotId", snapshot.name},
                {"hostId", catalog_host_id},
                {"profileId", options.profile_id},
                {"sourceId", source.id.value()},
                {"relativePath", (relative_root / snapshot.name).string()},
                {"createdAt", format_utc_iso_timestamp(snapshot.timestamp)},
                {"uuid", snapshot.uuid.value()},
                {"verified", true},
            };
            if (!snapshot.received_uuid.empty())
                entry["receivedUuid"] = snapshot.received_uuid.value();
            snapshots.push_back(std::move(entry));
        }
    }

    const std::string repository_json = config::json::dump_json({
        {"schemaVersion", restore::repository_format_version},
        {"repositoryId", repository_id},
        {"targetFilesystemUuid", mount.filesystem_uuid},
        {"createdAt", created_at},
        {"features", config::json::Json::array({"catalog-v1"})},
    });
    const std::string catalog_json = config::json::dump_json({
        {"schemaVersion", restore::catalog_format_version},
        {"generation", generation},
        {"snapshots", std::move(snapshots)},
    });
    std::println(output, "Repository: {}", root.string());
    std::println(output, "Filesystem UUID: {}", mount.filesystem_uuid);
    std::println(output, "Catalog generation: {}", generation);
    std::println(output, "Snapshots: {}", snapshot_ids.size());
    if (!options.apply) {
        std::println(output, "Dry run only. Repeat with --apply to write repository.json and catalog.json.");
        return 0;
    }
    platform::linux::filesystem::atomic_write(catalog_file, catalog_json, 0600);
    platform::linux::filesystem::atomic_write(repository_file, repository_json, 0600);
    std::println(output, "Repository metadata written atomically.");
    return 0;
}

} // namespace btrfsbackup::cli::repository
