// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <config/domain/ConfigurationGeneration.hpp>
#include <config/domain/OperationPath.hpp>
#include <config/domain/RepositoryPath.hpp>
#include <config/domain/StoragePolicy.hpp>
#include <config/domain/TargetIdentity.hpp>
#include <core/Identifiers.hpp>
namespace btrfsbackup::config {

struct AskPasswordActivation {
};

struct KeyFileActivation {
    KeyFilePath key_file;
};

using TargetActivation = std::variant<AskPasswordActivation, KeyFileActivation>;

struct ProfileTarget {
    ProfileTarget(
        LuksUuid luks_uuid_value,
        BtrfsUuid btrfs_uuid_value,
        PartitionUuid partition_uuid_value,
        MapperName mapper_name_value
    )
        : device(TargetDevicePath{"/dev"}),
          luks_uuid(std::move(luks_uuid_value)),
          btrfs_uuid(std::move(btrfs_uuid_value)),
          partition_uuid(std::move(partition_uuid_value)),
          mapper_name(std::move(mapper_name_value)),
          mount_point(TargetMountPoint{"/"}) {
    }

    TargetDevicePath device;
    LuksUuid luks_uuid;
    BtrfsUuid btrfs_uuid;
    PartitionUuid partition_uuid;
    std::string serial;
    MapperName mapper_name;
    TargetMountPoint mount_point;
    TargetActivation activation;
};

struct ProfilePaths {
    ProfilePaths(RemoteSnapshotRoot remote_root_value, IncomingRoot incoming_root_value)
        : remote_root(std::move(remote_root_value)), incoming_root(std::move(incoming_root_value)) {
    }

    RemoteSnapshotRoot remote_root;
    IncomingRoot incoming_root;
};

struct ProfileSettings {
    bool daily_limit = true;
    bool incremental_required = true;
    bool keep_failed_local_snapshot = false;
    bool auto_eject = true;
    RetentionCount remote_retention{30};
    RetentionCount local_retention{30};
    ByteThreshold minimum_target_free_bytes{0};
    ByteThreshold minimum_local_free_bytes{0};
};

struct ProfileHookCommand {
    HookProgramPath program;
    std::vector<std::string> arguments;
    std::chrono::seconds timeout{30};
};

struct ProfileHooks {
    std::vector<ProfileHookCommand> before_snapshot;
    std::vector<ProfileHookCommand> after_snapshot;
};

struct ProfileSource {
    explicit ProfileSource(SourceId identifier)
        : id(std::move(identifier)),
          subvolume(SourceSubvolumePath{"/"}),
          local_snapshot_dir(LocalSnapshotRoot{"/"}),
          remote_subdir(std::string(id.value())) {
    }

    ProfileSource(SourceId identifier, SafeRelativePath remote_subdir_value)
        : id(std::move(identifier)),
          subvolume(SourceSubvolumePath{"/"}),
          local_snapshot_dir(LocalSnapshotRoot{"/"}),
          remote_subdir(std::move(remote_subdir_value)) {
    }

    SourceId id;
    std::string name;
    bool enabled = true;
    SourceSubvolumePath subvolume;
    LocalSnapshotRoot local_snapshot_dir;
    SafeRelativePath remote_subdir;
    RetentionCount remote_retention{30};
    RetentionCount local_retention{30};
};

struct Profile {
    Profile(ProfileId identifier, ProfileTarget target_value, ProfilePaths paths_value)
        : id(std::move(identifier)), target(std::move(target_value)), paths(std::move(paths_value)) {
    }

    ConfigurationGeneration configuration_generation{""};
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
