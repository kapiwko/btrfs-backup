// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/wizard/profile_wizard_model.hpp>

#include <config/model/profile_document.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

#include <core/errors.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup::config {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

Profile profile_from_wizard_answers(const ProfileWizardAnswers& answers) {
    Profile profile{ProfileId{answers.profile_id}};
    profile.name = answers.profile_name;
    profile.enabled = true;

    profile.target.device = answers.target_device;
    profile.target.luks_uuid = lower(answers.target_luks_uuid);
    profile.target.btrfs_uuid = answers.target_btrfs_uuid;
    profile.target.partition_uuid = answers.target_partition_uuid;
    profile.target.serial = answers.target_serial;
    profile.target.mapper_name = answers.target_mapper_name;
    validate_identifier(profile.target.mapper_name, "target.mapperName");
    profile.target.mount_point = (std::filesystem::path(answers.target_mount_root) / profile.id.value()).string();

    profile.paths.remote_root = profile.target.mount_point + "/snapshots";
    profile.paths.incoming_root = profile.target.mount_point + "/.incoming";

    std::set<std::string> used_names;
    for (const ProfileWizardSourceAnswers& source_answer : answers.sources) {
        ProfileSource source{SourceId{source_answer.id}};
        const std::string source_id{source.id.value()};
        if (!used_names.insert(source_id).second) {
            throw ValidationError("duplicate source name: " + source_id);
        }
        source.name = source_id;
        source.enabled = true;
        source.subvolume = source_answer.subvolume;
        source.local_snapshot_dir = source_answer.local_snapshot_dir;
        source.remote_subdir = source_answer.remote_subdir;
        source.remote_retention = answers.remote_retention;
        source.local_retention = answers.local_retention;
        profile.sources.push_back(source);
    }

    profile.settings.remote_retention = answers.remote_retention;
    profile.settings.local_retention = answers.local_retention;
    profile.settings.daily_limit = answers.daily_limit;
    profile.settings.incremental_required = answers.incremental_required;
    profile.settings.keep_failed_local_snapshot = answers.keep_failed_local_snapshot;
    profile.settings.auto_eject = answers.auto_eject;
    profile.settings.minimum_target_free_bytes = answers.minimum_target_free_bytes;
    profile.settings.minimum_local_free_bytes = answers.minimum_local_free_bytes;

    return profile_from_json(profile_to_json(profile), answers.target_mount_root);
}

} // namespace btrfsbackup::config
