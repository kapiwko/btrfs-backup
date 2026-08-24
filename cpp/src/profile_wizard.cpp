#include <btrfsbackup/profile_wizard.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <btrfsbackup/installation_validate.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/profile_wizard_device.hpp>
#include <btrfsbackup/profile_wizard_install.hpp>
#include <btrfsbackup/profile_wizard_model.hpp>
#include <btrfsbackup/profile_wizard_paths.hpp>
#include <btrfsbackup/profile_wizard_prompt.hpp>
#include <btrfsbackup/profile_wizard_sources.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

ProfileWizardAnswers collect_answers(std::istream& input, std::ostream& output) {
    wizard::DeviceCandidate device = wizard::select_device(input, output);

    ProfileWizardAnswers answers;
    answers.profile_id = wizard::prompt_value(input, output, "Profile identifier", "default");
    answers.profile_name = wizard::prompt_value(input, output, "Profile display name", answers.profile_id);

    answers.target_device = wizard::best_device_reference(device);
    answers.target_luks_uuid = device.uuid;
    answers.target_partition_uuid = wizard::udev_property_for_device(device.path, "ID_PART_ENTRY_UUID");
    answers.target_serial = wizard::udev_property_for_device(device.path, "ID_SERIAL_SHORT");
    answers.target_mapper_name = wizard::prompt_value(input, output, "LUKS mapper name", "backupdisk");
    answers.target_mount_point = wizard::prompt_value(input, output, "Backup mountpoint", "/mnt/backup");
    answers.target_btrfs_uuid = wizard::prompt_value(
        input,
        output,
        "Expected Btrfs UUID inside LUKS (empty disables this additional check)",
        wizard::detect_btrfs_uuid(answers.target_mapper_name)
    );

    std::vector<std::string> source_paths = wizard::select_sources(input, output);
    std::vector<std::string> used_names;
    for (const auto& source_path : source_paths) {
        std::string default_name = wizard::source_name_from_path(source_path);
        while (std::find(used_names.begin(), used_names.end(), default_name) != used_names.end()) {
            default_name += "-2";
        }
        ProfileWizardSourceAnswers source;
        source.id = wizard::prompt_value(input, output, "Source name for " + source_path, default_name);
        used_names.push_back(source.id);
        source.subvolume = source_path;
        source.local_snapshot_dir = wizard::prompt_value(input, output, "Local snapshot directory for " + source_path, "/.snapshots/btrfs-backup/" + source.id);
        source.remote_subdir = wizard::prompt_value(input, output, "Remote subdirectory under the backup snapshots root", source.id);
        answers.sources.push_back(source);
    }

    answers.remote_retention = wizard::prompt_uint(input, output, "Remote retention count; 0 means unlimited", 30);
    answers.local_retention = wizard::prompt_uint(input, output, "Local retention count; 0 means unlimited", 30);
    answers.daily_limit = wizard::prompt_bool(input, output, "Run at most once per local calendar day", true);
    answers.incremental_required = wizard::prompt_bool(input, output, "Fail instead of silently starting a new full chain when remote snapshots exist", true);
    answers.keep_failed_local_snapshot = wizard::prompt_bool(input, output, "Keep a new local snapshot after a failed transfer", false);
    answers.auto_eject = wizard::prompt_bool(input, output, "Unmount and close LUKS automatically after the service finishes", true);
    answers.minimum_target_free_bytes = wizard::prompt_uint(input, output, "Minimum free bytes required on the backup target; 0 disables", 5368709120LL);
    answers.minimum_local_free_bytes = wizard::prompt_uint(input, output, "Minimum free bytes required for local snapshots; 0 disables", 1073741824LL);
    answers.keyfile = wizard::prompt_value(input, output, "crypttab keyfile path or none", "/root/keys/" + answers.target_mapper_name + ".key");

    return answers;
}

} // namespace

int run_profile_wizard(const ProfileWizardOptions& options, std::istream& input, std::ostream& output) {
    if (options.action == ProfileWizardAction::validate_active) {
        validate_active_installation(options.profile_id);
        return 0;
    }
    if (options.action == ProfileWizardAction::validate_rendered) {
        validate_rendered_installation(options.validate_dir);
        return 0;
    }

    ProfileWizardAnswers answers = collect_answers(input, output);
    Profile profile = profile_from_wizard_answers(answers);
    fs::path output_dir = options.output_dir.empty() ? wizard::default_output_dir() : options.output_dir;
    output_dir = fs::absolute(output_dir).lexically_normal();
    render_wizard_tree(profile, answers.keyfile, output_dir);

    if (options.action == ProfileWizardAction::apply) {
        apply_rendered_wizard_tree(profile, output_dir);
        output << "Installed active configuration for profile " << profile.id << "\n";
    } else {
        output << "Rendered and validated files in:\n  " << output_dir << "\n";
        output << "\nReview them, merge the fstab/crypttab fragments manually, or rerun with --apply as root.\n";
    }
    return 0;
}

} // namespace btrfsbackup
