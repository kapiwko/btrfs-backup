#include <btrfsbackup/profile_wizard_model.hpp>

#include <algorithm>
#include <cctype>
#include <set>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/identifiers.hpp>

namespace btrfsbackup {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

Profile profile_from_wizard_answers(const ProfileWizardAnswers& answers) {
    Profile profile;
    profile.schema_version = 1;
    profile.id = answers.profile_id;
    validate_identifier(profile.id, "profileId");
    profile.name = answers.profile_name;
    profile.enabled = true;

    profile.target.device = answers.target_device;
    profile.target.luks_uuid = lower(answers.target_luks_uuid);
    profile.target.btrfs_uuid = answers.target_btrfs_uuid;
    profile.target.partition_uuid = answers.target_partition_uuid;
    profile.target.serial = answers.target_serial;
    profile.target.mapper_name = answers.target_mapper_name;
    validate_identifier(profile.target.mapper_name, "target.mapperName");
    profile.target.mount_point = answers.target_mount_point;

    profile.paths.remote_root = profile.target.mount_point + "/snapshots";
    profile.paths.incoming_root = profile.target.mount_point + "/.incoming";
    profile.paths.sources_dir = "/etc/btrfs-backup/profiles/" + profile.id + "/sources.d";
    profile.paths.state_dir = "/var/lib/btrfs-backup";
    profile.paths.status_root = "/run/btrfs-backup/profiles";
    profile.paths.history_root = "/var/lib/btrfs-backup/history";

    std::set<std::string> used_names;
    for (const ProfileWizardSourceAnswers& source_answer : answers.sources) {
        ProfileSource source;
        source.id = source_answer.id;
        validate_identifier(source.id, "source.id");
        if (!used_names.insert(source.id).second) {
            throw ValidationError("duplicate source name: " + source.id);
        }
        source.name = source.id;
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

    profile.notifications.enabled = answers.notifications_enabled;
    profile.notifications.user = answers.notifications_user;
    profile.notifications.method = answers.notifications_method;

    return profile_from_json(profile_to_json(profile));
}

} // namespace btrfsbackup
