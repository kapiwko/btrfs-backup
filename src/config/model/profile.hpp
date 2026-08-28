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

#include <core/identifiers.hpp>
namespace btrfsbackup::config {

struct ProfileTarget {
    std::string device;
    std::string luks_uuid;
    std::string btrfs_uuid;
    std::string partition_uuid;
    std::string serial;
    std::string mapper_name;
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
    explicit Profile(ProfileId identifier) : id(std::move(identifier)) {
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
