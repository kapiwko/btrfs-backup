// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/wizard/profile_wizard_model.hpp>

#include <config/model/profile_document.hpp>

#include <filesystem>
#include <set>

#include <core/errors.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup::config {

Profile profile_from_wizard_answers(const ProfileWizardAnswers& answers) {
    Profile profile{
        ProfileId{answers.profile_id},
        ProfileTarget{
            LuksUuid{answers.target_luks_uuid},
            BtrfsUuid{answers.target_btrfs_uuid},
            PartitionUuid{answers.target_partition_uuid},
            MapperName{answers.target_mapper_name},
        },
        ProfilePaths{
            RemoteSnapshotRoot{(std::filesystem::path(answers.target_mount_root) / answers.profile_id / "snapshots").string()},
            IncomingRoot{(std::filesystem::path(answers.target_mount_root) / answers.profile_id / ".incoming").string()},
        },
    };
    profile.name = answers.profile_name;
    profile.enabled = true;

    profile.target.device = TargetDevicePath{answers.target_device};
    profile.target.serial = answers.target_serial;
    profile.target.mount_point = TargetMountPoint{
        std::filesystem::path(answers.target_mount_root) / profile.id.value()
    };
    if (answers.keyfile != "none") {
        profile.target.activation.mode = TargetActivationMode::KeyFile;
        profile.target.activation.key_file = answers.keyfile;
    }

    std::set<std::string> used_names;
    for (const ProfileWizardSourceAnswers& source_answer : answers.sources) {
        ProfileSource source{SourceId{source_answer.id}, SafeRelativePath{source_answer.remote_subdir}};
        const std::string source_id{source.id.value()};
        if (!used_names.insert(source_id).second) {
            throw ValidationError("duplicate source name: " + source_id);
        }
        source.name = source_id;
        source.enabled = true;
        source.subvolume = SourceSubvolumePath{source_answer.subvolume};
        source.local_snapshot_dir = LocalSnapshotRoot{source_answer.local_snapshot_dir};
        source.remote_retention = RetentionCount{answers.remote_retention};
        source.local_retention = RetentionCount{answers.local_retention};
        profile.sources.push_back(source);
    }

    profile.settings.remote_retention = RetentionCount{answers.remote_retention};
    profile.settings.local_retention = RetentionCount{answers.local_retention};
    profile.settings.daily_limit = answers.daily_limit;
    profile.settings.incremental_required = answers.incremental_required;
    profile.settings.keep_failed_local_snapshot = answers.keep_failed_local_snapshot;
    profile.settings.auto_eject = answers.auto_eject;
    profile.settings.minimum_target_free_bytes = ByteThreshold{answers.minimum_target_free_bytes};
    profile.settings.minimum_local_free_bytes = ByteThreshold{answers.minimum_local_free_bytes};

    return profile_from_json(profile_to_json(profile), answers.target_mount_root);
}

} // namespace btrfsbackup::config
