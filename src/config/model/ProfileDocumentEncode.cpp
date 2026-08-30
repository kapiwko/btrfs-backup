// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/model/ProfileDocument.hpp>

#include <utility>
#include <vector>

namespace btrfsbackup::config {

Json profile_to_json(const Profile& profile) {
    Json sources = Json::array();
    for (const ProfileSource& source : profile.sources) {
        sources.push_back({{"id", source.id.value()}, {"name", source.name}, {"enabled", source.enabled}, {"subvolume", source.subvolume.value().string()}, {"localSnapshotDir", source.local_snapshot_dir.value().string()}, {"remoteSubdir", source.remote_subdir.value().string()}, {"remoteRetention", source.remote_retention.value()}, {"localRetention", source.local_retention.value()}});
    }

    Json activation = {{
        "mode",
        profile.target.activation.mode == TargetActivationMode::KeyFile ? "keyFile" : "askPassword",
    }};
    if (profile.target.activation.mode == TargetActivationMode::KeyFile) {
        activation["keyFile"] = profile.target.activation.key_file.string();
    }

    Json target = {
        {"device", profile.target.device.value().string()},
        {"luksUuid", profile.target.luks_uuid.value()},
        {"btrfsUuid", profile.target.btrfs_uuid.value()},
        {"partitionUuid", profile.target.partition_uuid.value()},
        {"serial", profile.target.serial},
        {"mapperName", profile.target.mapper_name.value()},
        {"activation", std::move(activation)}
    };

    auto hooks_to_json = [](const std::vector<ProfileHookCommand>& hooks) {
        Json result = Json::array();
        for (const ProfileHookCommand& hook : hooks) {
            result.push_back({{"type", "program"}, {"program", hook.program.value().string()}, {"arguments", hook.arguments}, {"timeoutSeconds", hook.timeout.count()}});
        }
        return result;
    };

    Json result = {
        {"schemaVersion", current_profile_schema_version},
        {"profileId", profile.id.value()},
        {"name", profile.name},
        {"enabled", profile.enabled},
        {"target", target},
        {"paths", {{"remoteRoot", profile.paths.remote_root.value().string()}, {"incomingRoot", profile.paths.incoming_root.value().string()}}},
        {"settings", {{"dailyLimit", profile.settings.daily_limit}, {"incrementalRequired", profile.settings.incremental_required}, {"keepFailedLocalSnapshot", profile.settings.keep_failed_local_snapshot}, {"autoEject", profile.settings.auto_eject}, {"remoteRetention", profile.settings.remote_retention.value()}, {"localRetention", profile.settings.local_retention.value()}, {"minimumTargetFreeBytes", profile.settings.minimum_target_free_bytes.value()}, {"minimumLocalFreeBytes", profile.settings.minimum_local_free_bytes.value()}}},
        {"hooks", {{"beforeSnapshot", hooks_to_json(profile.hooks.before_snapshot)}, {"afterSnapshot", hooks_to_json(profile.hooks.after_snapshot)}}},
        {"sources", sources}
    };
    if (!profile.configuration_generation.empty()) {
        result["configurationGeneration"] = profile.configuration_generation.value();
    }
    return result;
}

ProfileDocument profile_to_document(const Profile& profile) {
    return ProfileDocument{profile_to_json(profile)};
}

} // namespace btrfsbackup::config
