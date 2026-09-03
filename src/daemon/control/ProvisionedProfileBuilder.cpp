// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/ProvisionedProfileBuilder.hpp>

#include <utility>

#include <config/wizard/ProfileWizardModel.hpp>

namespace btrfsbackup::daemon::control {

ProvisionedProfileBuilder::ProvisionedProfileBuilder(std::filesystem::path target_mount_root)
    : target_mount_root_(std::move(target_mount_root)) {
}

config::Profile ProvisionedProfileBuilder::build(
    const provisioning::DevicePreparationTransaction& transaction,
    const std::string& luks_uuid,
    const std::string& btrfs_uuid,
    const std::string& partition_uuid
) const {
    config::wizard::ProfileWizardAnswers answers;
    answers.profile_id = transaction.status.profile_id;
    answers.profile_name = transaction.profile_name;
    answers.target_device = "/dev/disk/by-uuid/" + luks_uuid;
    answers.target_luks_uuid = luks_uuid;
    answers.target_btrfs_uuid = btrfs_uuid;
    answers.target_partition_uuid = partition_uuid;
    answers.target_serial = transaction.device.serial_short.empty()
        ? transaction.device.serial
        : transaction.device.serial_short;
    answers.target_mapper_name = "backupdisk-" + transaction.status.profile_id;
    answers.target_mount_root = target_mount_root_.string();
    answers.keyfile = "none";
    answers.sources.push_back({
        .id = "source",
        .subvolume = transaction.source_subvolume,
        .local_snapshot_dir = transaction.local_snapshot_dir,
        .remote_subdir = "source",
    });
    config::Profile profile = config::wizard::profile_from_wizard_answers(answers);
    profile.enabled = transaction.create_automatic_key;
    return profile;
}

} // namespace btrfsbackup::daemon::control
