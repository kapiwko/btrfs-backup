// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <config/domain/Profile.hpp>

namespace btrfsbackup::config::wizard {

struct ProfileWizardSourceAnswers {
    std::string id;
    std::string name;
    std::string subvolume;
    std::string local_snapshot_dir;
    std::string remote_subdir;
    std::optional<std::size_t> local_retention;
    std::optional<std::size_t> remote_retention;
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

    std::size_t remote_retention = 30;
    std::size_t local_retention = 30;
    bool daily_limit = true;
    bool incremental_required = true;
    bool keep_failed_local_snapshot = false;
    bool auto_eject = true;
    std::uint64_t minimum_target_free_bytes = 5ULL * 1024 * 1024 * 1024;
    std::uint64_t minimum_local_free_bytes = 1024ULL * 1024 * 1024;

    std::string keyfile = "none";
};

Profile profile_from_wizard_answers(const ProfileWizardAnswers& answers);

} // namespace btrfsbackup::config::wizard
