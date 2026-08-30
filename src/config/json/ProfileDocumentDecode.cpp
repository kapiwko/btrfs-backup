// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/json/ProfileDocument.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <core/Identifiers.hpp>
#include <config/domain/Validation.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::config::json {

Profile profile_from_document(const ProfileDocument& document, const fs::path& target_mount_root) {
    const Json normalized = normalize_profile(document.value, target_mount_root);
    const Json& target = normalized.at("target");
    Profile profile{
        ProfileId{normalized.at("profileId").get<std::string>()},
        ProfileTarget{
            LuksUuid{target.at("luksUuid").get<std::string>()},
            BtrfsUuid{target.at("btrfsUuid").get<std::string>()},
            PartitionUuid{target.at("partitionUuid").get<std::string>()},
            MapperName{target.at("mapperName").get<std::string>()},
        },
        ProfilePaths{
            RemoteSnapshotRoot{normalized.at("paths").at("remoteRoot").get<std::string>()},
            IncomingRoot{normalized.at("paths").at("incomingRoot").get<std::string>()},
        },
    };
    profile.configuration_generation = ConfigurationGeneration{normalized.value("configurationGeneration", "")};
    profile.name = normalized.at("name").get<std::string>();
    profile.enabled = normalized.at("enabled").get<bool>();

    profile.target.device = TargetDevicePath{target.at("device").get<std::string>()};
    profile.target.serial = target.at("serial").get<std::string>();
    profile.target.mount_point = TargetMountPoint{
        normalized_absolute_path(target_mount_root, "TARGET_MOUNT_ROOT") / profile.id.value()
    };
    const Json& activation = target.at("activation");
    if (activation.at("mode") == "keyFile") {
        profile.target.activation = KeyFileActivation{
            KeyFilePath{activation.at("keyFile").get<std::string>()},
        };
    }

    const Json& settings = normalized.at("settings");
    profile.settings.daily_limit = settings.at("dailyLimit").get<bool>();
    profile.settings.incremental_required = settings.at("incrementalRequired").get<bool>();
    profile.settings.keep_failed_local_snapshot = settings.at("keepFailedLocalSnapshot").get<bool>();
    profile.settings.auto_eject = settings.at("autoEject").get<bool>();
    profile.settings.remote_retention = RetentionCount{settings.at("remoteRetention").get<std::uint64_t>()};
    profile.settings.local_retention = RetentionCount{settings.at("localRetention").get<std::uint64_t>()};
    profile.settings.minimum_target_free_bytes = ByteThreshold{settings.at("minimumTargetFreeBytes").get<std::uint64_t>()};
    profile.settings.minimum_local_free_bytes = ByteThreshold{settings.at("minimumLocalFreeBytes").get<std::uint64_t>()};

    const Json& hooks = normalized.at("hooks");
    for (const Json& item : hooks.at("beforeSnapshot")) {
        profile.hooks.before_snapshot.push_back({
            .program = HookProgramPath{item.at("program").get<std::string>()},
            .arguments = item.at("arguments").get<std::vector<std::string>>(),
            .timeout = std::chrono::seconds{item.at("timeoutSeconds").get<std::chrono::seconds::rep>()},
        });
    }
    for (const Json& item : hooks.at("afterSnapshot")) {
        profile.hooks.after_snapshot.push_back({
            .program = HookProgramPath{item.at("program").get<std::string>()},
            .arguments = item.at("arguments").get<std::vector<std::string>>(),
            .timeout = std::chrono::seconds{item.at("timeoutSeconds").get<std::chrono::seconds::rep>()},
        });
    }

    for (const Json& item : normalized.at("sources")) {
        ProfileSource source{
            SourceId{item.at("id").get<std::string>()},
            SafeRelativePath{item.at("remoteSubdir").get<std::string>()},
        };
        source.name = item.at("name").get<std::string>();
        source.enabled = item.at("enabled").get<bool>();
        source.subvolume = SourceSubvolumePath{item.at("subvolume").get<std::string>()};
        source.local_snapshot_dir = LocalSnapshotRoot{item.at("localSnapshotDir").get<std::string>()};
        source.remote_retention = RetentionCount{item.at("remoteRetention").get<std::uint64_t>()};
        source.local_retention = RetentionCount{item.at("localRetention").get<std::uint64_t>()};
        profile.sources.push_back(std::move(source));
    }
    return profile;
}

Profile profile_from_json(const Json& raw, const fs::path& target_mount_root) {
    return profile_from_document(normalize_profile_document(raw, target_mount_root), target_mount_root);
}

} // namespace btrfsbackup::config::json
