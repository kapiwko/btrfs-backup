#pragma once

#include <string>
#include <vector>

#include <config/profile.hpp>

namespace btrfsbackup {

struct ProfileWizardSourceAnswers {
    std::string id;
    std::string subvolume;
    std::string local_snapshot_dir;
    std::string remote_subdir;
};

struct ProfileWizardAnswers {
    std::string profile_id = "default";
    std::string profile_name = "default";

    std::string target_device;
    std::string target_luks_uuid;
    std::string target_btrfs_uuid;
    std::string target_partition_uuid;
    std::string target_serial;
    std::string target_mapper_name = "backupdisk";
    std::string target_mount_root = "/mnt/btrfs-backup";

    std::vector<ProfileWizardSourceAnswers> sources;

    long long remote_retention = 30;
    long long local_retention = 30;
    bool daily_limit = true;
    bool incremental_required = true;
    bool keep_failed_local_snapshot = false;
    bool auto_eject = true;
    long long minimum_target_free_bytes = 5LL * 1024 * 1024 * 1024;
    long long minimum_local_free_bytes = 1024LL * 1024 * 1024;

    std::string keyfile = "none";

};

Profile profile_from_wizard_answers(const ProfileWizardAnswers& answers);

} // namespace btrfsbackup
