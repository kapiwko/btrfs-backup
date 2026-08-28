// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <config/model/target_identity.hpp>
#include <core/identifiers.hpp>
namespace btrfsbackup::config {

struct ProfileTarget {
    ProfileTarget(
        LuksUuid luks_uuid_value,
        BtrfsUuid btrfs_uuid_value,
        PartitionUuid partition_uuid_value,
        MapperName mapper_name_value
    )
        : luks_uuid(std::move(luks_uuid_value)),
          btrfs_uuid(std::move(btrfs_uuid_value)),
          partition_uuid(std::move(partition_uuid_value)),
          mapper_name(std::move(mapper_name_value)) {
    }

    std::string device;
    LuksUuid luks_uuid;
    BtrfsUuid btrfs_uuid;
    PartitionUuid partition_uuid;
    std::string serial;
    MapperName mapper_name;
    std::string mount_point;
};

struct ProfilePaths {
    std::string remote_root;
    std::string incoming_root;
};

struct ProfileSettings {
    bool daily_limit = true;
    bool incremental_required = true;
    bool keep_failed_local_snapshot = false;
    bool auto_eject = true;
    std::size_t remote_retention = 30;
    std::size_t local_retention = 30;
    std::uint64_t minimum_target_free_bytes = 0;
    std::uint64_t minimum_local_free_bytes = 0;
};

struct ProfileHookCommand {
    std::string program;
    std::vector<std::string> arguments;
    std::chrono::seconds timeout{30};
};

struct ProfileHooks {
    std::vector<ProfileHookCommand> before_snapshot;
    std::vector<ProfileHookCommand> after_snapshot;
};

struct ProfileSource {
    explicit ProfileSource(SourceId identifier) : id(std::move(identifier)) {
    }

    SourceId id;
    std::string name;
    bool enabled = true;
    std::string subvolume;
    std::string local_snapshot_dir;
    std::string remote_subdir;
    std::size_t remote_retention = 30;
    std::size_t local_retention = 30;
};

struct Profile {
    Profile(ProfileId identifier, ProfileTarget target_value)
        : id(std::move(identifier)), target(std::move(target_value)) {
    }

    std::string configuration_generation;
    ProfileId id;
    std::string name;
    bool enabled = true;
    ProfileTarget target;
    ProfilePaths paths;
    ProfileSettings settings;
    ProfileHooks hooks;
    std::vector<ProfileSource> sources;
};

} // namespace btrfsbackup::config
