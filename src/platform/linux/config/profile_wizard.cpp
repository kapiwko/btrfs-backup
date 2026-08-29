// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/profile_wizard.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <platform/linux/config/installation_validate.hpp>
#include <platform/linux/config/application_config.hpp>
#include <config/model/profile.hpp>
#include <platform/linux/config/profile_wizard_device.hpp>
#include <platform/linux/config/profile_wizard_install.hpp>
#include <config/wizard/profile_wizard_model.hpp>
#include <config/wizard/profile_wizard_paths.hpp>
#include <config/wizard/profile_wizard_prompt.hpp>
#include <platform/linux/config/profile_wizard_sources.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux {

namespace {

btrfsbackup::config::ProfileWizardAnswers collect_answers(std::istream& input, std::ostream& output) {
    DeviceCandidate device = select_device(input, output);

    btrfsbackup::config::ProfileWizardAnswers answers;
    answers.target_mount_root = load_application_config().paths().target_mount_root.string();
    answers.profile_id = btrfsbackup::config::prompt_value(input, output, "Profile identifier", "default");
    answers.profile_name = btrfsbackup::config::prompt_value(input, output, "Profile display name", answers.profile_id);

    answers.target_device = best_device_reference(device);
    answers.target_luks_uuid = device.uuid;
    answers.target_partition_uuid = udev_property_for_device(device.path, "ID_PART_ENTRY_UUID");
    answers.target_serial = udev_property_for_device(device.path, "ID_SERIAL_SHORT");
    answers.target_mapper_name = btrfsbackup::config::prompt_value(input, output, "LUKS mapper name", "backupdisk");
    answers.target_btrfs_uuid = btrfsbackup::config::prompt_value(
        input,
        output,
        "Expected Btrfs UUID inside LUKS (empty disables this additional check)",
        detect_btrfs_uuid(answers.target_mapper_name)
    );

    std::vector<std::string> source_paths = select_sources(input, output);
    std::vector<std::string> used_names;
    for (const auto& source_path : source_paths) {
        std::string default_name = source_name_from_path(source_path);
        while (std::find(used_names.begin(), used_names.end(), default_name) != used_names.end()) {
            default_name += "-2";
        }
        btrfsbackup::config::ProfileWizardSourceAnswers source;
        source.id = btrfsbackup::config::prompt_value(input, output, "Source name for " + source_path, default_name);
        used_names.push_back(source.id);
        source.subvolume = source_path;
        source.local_snapshot_dir = btrfsbackup::config::prompt_value(
            input,
            output,
            "Local snapshot directory for " + source_path,
            "/.snapshots/btrfs-backup/" + source.id
        );
        source.remote_subdir = btrfsbackup::config::prompt_value(input, output, "Remote subdirectory under the backup snapshots root", source.id);
        answers.sources.push_back(source);
    }

    answers.remote_retention = static_cast<std::size_t>(
        btrfsbackup::config::prompt_uint(input, output, "Remote retention count; 0 means unlimited", 30)
    );
    answers.local_retention = static_cast<std::size_t>(
        btrfsbackup::config::prompt_uint(input, output, "Local retention count; 0 means unlimited", 30)
    );
    answers.daily_limit = btrfsbackup::config::prompt_bool(input, output, "Run at most once per local calendar day", true);
    answers.incremental_required = btrfsbackup::config::prompt_bool(input, output, "Fail instead of silently starting a new full chain when remote snapshots exist", true);
    answers.keep_failed_local_snapshot = btrfsbackup::config::prompt_bool(input, output, "Keep a new local snapshot after a failed transfer", false);
    answers.auto_eject = btrfsbackup::config::prompt_bool(input, output, "Unmount and close LUKS automatically after the service finishes", true);
    answers.minimum_target_free_bytes = btrfsbackup::config::prompt_uint(input, output, "Minimum free bytes required on the backup target; 0 disables", 5368709120ULL);
    answers.minimum_local_free_bytes = btrfsbackup::config::prompt_uint(input, output, "Minimum free bytes required for local snapshots; 0 disables", 1073741824ULL);
    answers.keyfile = btrfsbackup::config::prompt_value(input, output, "crypttab keyfile path or none", "/root/keys/" + answers.target_mapper_name + ".key");

    return answers;
}

} // namespace

int run_profile_wizard(const ProfileWizardOptions& options, std::istream& input, std::ostream& output) {
    if (options.action == ProfileWizardAction::validate_active) {
        validate_active_installation(options.profile_id);
        return 0;
    }
    if (options.action == ProfileWizardAction::validate_rendered) {
        validate_rendered_installation(
            options.validate_dir,
            load_application_config().paths().target_mount_root
        );
        return 0;
    }

    btrfsbackup::config::ProfileWizardAnswers answers = collect_answers(input, output);
    btrfsbackup::config::Profile profile = btrfsbackup::config::profile_from_wizard_answers(answers);
    fs::path output_dir = options.output_dir.empty() ? btrfsbackup::config::default_output_dir() : options.output_dir;
    output_dir = fs::absolute(output_dir).lexically_normal();
    render_wizard_tree(profile, answers.keyfile, output_dir);

    if (options.action == ProfileWizardAction::apply) {
        apply_rendered_wizard_tree(profile, output_dir);
        output << "Installed active configuration for profile " << profile.id.value() << "\n";
    } else {
        output << "Rendered and validated files in:\n  " << output_dir << "\n";
        output << "\nReview them, merge the fstab/crypttab fragments manually, or rerun with --apply as root.\n";
    }
    return 0;
}

} // namespace btrfsbackup::platform::linux
