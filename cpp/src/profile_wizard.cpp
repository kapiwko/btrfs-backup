#include <btrfsbackup/profile_wizard.hpp>

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/identifiers.hpp>
#include <btrfsbackup/installation_render.hpp>
#include <btrfsbackup/installation_validate.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/process.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/profile_store.hpp>
#include <btrfsbackup/profile_wizard_device.hpp>
#include <btrfsbackup/profile_wizard_paths.hpp>
#include <btrfsbackup/profile_wizard_prompt.hpp>
#include <btrfsbackup/profile_wizard_sources.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

struct WizardAnswers {
    Profile profile;
    std::string keyfile = "none";
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string default_user() {
    if (const char* sudo_user = std::getenv("SUDO_USER"); sudo_user && *sudo_user) {
        return sudo_user;
    }
    if (const char* user = std::getenv("USER"); user && *user) {
        return user;
    }
    return "root";
}

Profile build_profile(const WizardAnswers& answers) {
    return profile_from_json(profile_to_json(answers.profile));
}

WizardAnswers collect_answers(std::istream& input, std::ostream& output) {
    wizard::DeviceCandidate device = wizard::select_device(input, output);

    WizardAnswers answers;
    Profile profile;
    profile.schema_version = 1;
    profile.id = wizard::prompt_value(input, output, "Profile identifier", "default");
    validate_identifier(profile.id, "profileId");
    profile.name = wizard::prompt_value(input, output, "Profile display name", profile.id);
    profile.enabled = true;

    profile.target.device = wizard::best_device_reference(device);
    profile.target.luks_uuid = lower(device.uuid);
    profile.target.partition_uuid = wizard::udev_property_for_device(device.path, "ID_PART_ENTRY_UUID");
    profile.target.serial = wizard::udev_property_for_device(device.path, "ID_SERIAL_SHORT");
    profile.target.mapper_name = wizard::prompt_value(input, output, "LUKS mapper name", "backupdisk");
    validate_identifier(profile.target.mapper_name, "target.mapperName");
    profile.target.mount_point = wizard::prompt_value(input, output, "Backup mountpoint", "/mnt/backup");
    profile.target.btrfs_uuid = wizard::prompt_value(
        input,
        output,
        "Expected Btrfs UUID inside LUKS (empty disables this additional check)",
        wizard::detect_btrfs_uuid(profile.target.mapper_name)
    );

    profile.paths.remote_root = profile.target.mount_point + "/snapshots";
    profile.paths.incoming_root = profile.target.mount_point + "/.incoming";
    profile.paths.sources_dir = "/etc/btrfs-backup/profiles/" + profile.id + "/sources.d";
    profile.paths.state_dir = "/var/lib/btrfs-backup";
    profile.paths.status_root = "/run/btrfs-backup/profiles";
    profile.paths.history_root = "/var/lib/btrfs-backup/history";

    std::vector<std::string> source_paths = wizard::select_sources(input, output);
    std::set<std::string> used_names;
    for (const auto& source_path : source_paths) {
        std::string default_name = wizard::source_name_from_path(source_path);
        while (used_names.count(default_name) > 0) {
            default_name += "-2";
        }
        ProfileSource source;
        source.id = wizard::prompt_value(input, output, "Source name for " + source_path, default_name);
        validate_identifier(source.id, "source.id");
        if (!used_names.insert(source.id).second) {
            throw ValidationError("duplicate source name: " + source.id);
        }
        source.name = source.id;
        source.enabled = true;
        source.subvolume = source_path;
        source.local_snapshot_dir = wizard::prompt_value(input, output, "Local snapshot directory for " + source_path, "/.snapshots/btrfs-backup/" + source.id);
        source.remote_subdir = wizard::prompt_value(input, output, "Remote subdirectory under the backup snapshots root", source.id);
        profile.sources.push_back(source);
    }

    profile.settings.remote_retention = wizard::prompt_uint(input, output, "Remote retention count; 0 means unlimited", 30);
    profile.settings.local_retention = wizard::prompt_uint(input, output, "Local retention count; 0 means unlimited", 30);
    for (auto& source : profile.sources) {
        source.remote_retention = profile.settings.remote_retention;
        source.local_retention = profile.settings.local_retention;
    }
    profile.settings.daily_limit = wizard::prompt_bool(input, output, "Run at most once per local calendar day", true);
    profile.settings.incremental_required = wizard::prompt_bool(input, output, "Fail instead of silently starting a new full chain when remote snapshots exist", true);
    profile.settings.keep_failed_local_snapshot = wizard::prompt_bool(input, output, "Keep a new local snapshot after a failed transfer", false);
    profile.settings.auto_eject = wizard::prompt_bool(input, output, "Unmount and close LUKS automatically after the service finishes", true);
    profile.settings.minimum_target_free_bytes = wizard::prompt_uint(input, output, "Minimum free bytes required on the backup target; 0 disables", 5368709120LL);
    profile.settings.minimum_local_free_bytes = wizard::prompt_uint(input, output, "Minimum free bytes required for local snapshots; 0 disables", 1073741824LL);
    answers.keyfile = wizard::prompt_value(input, output, "crypttab keyfile path or none", "/root/keys/" + profile.target.mapper_name + ".key");

    profile.notifications.enabled = wizard::prompt_bool(input, output, "Enable notifications", true);
    profile.notifications.user = wizard::prompt_value(input, output, "Desktop notification user", default_user());
    profile.notifications.method = wizard::prompt_value(input, output, "Notification method: auto, desktop, journal, or none", "auto");

    answers.profile = build_profile({profile, answers.keyfile});
    return answers;
}

std::string detect_runtime_script(const std::string& filename) {
    fs::path repo_candidate = fs::current_path() / "scripts" / filename;
    if (fs::exists(repo_candidate)) {
        return repo_candidate.string();
    }
    fs::path installed = fs::path("/usr/lib/btrfs-backup") / filename;
    return installed.string();
}

void render_wizard_tree(const WizardAnswers& answers, const fs::path& output_dir) {
    wizard::assert_safe_output_dir(output_dir);
    std::error_code ec;
    fs::remove_all(output_dir, ec);
    fs::create_directories(output_dir / "config");
    fs::create_directories(output_dir / "systemd");
    fs::create_directories(output_dir / "udev");

    atomic_write(output_dir / "config" / "profile.json", dump_json(profile_to_json(answers.profile)), 0600);
    save_tree(answers.profile, output_dir / "config", output_dir / "udev", output_dir / "public" / "profiles");
    fs::path profile_rule = output_dir / "udev" / ("99-btrfs-backup-" + answers.profile.id + ".rules");
    if (fs::exists(profile_rule)) {
        fs::copy_file(profile_rule, output_dir / "udev" / "99-btrfs-backup.rules", fs::copy_options::overwrite_existing);
    }

    render_installation_files(
        answers.profile,
        output_dir,
        {
            detect_runtime_script("btrfs-backup.sh"),
            detect_runtime_script("btrfs-backup-eject.sh"),
            answers.keyfile
        }
    );
    validate_rendered_installation(output_dir);
}

void apply_rendered_tree(const WizardAnswers& answers, const fs::path& output_dir) {
    if (geteuid() != 0) {
        throw ValidationError("apply must be run as root");
    }
    fs::create_directories("/etc/btrfs-backup");
    fs::create_directories("/etc/systemd/system");
    fs::create_directories("/etc/udev/rules.d");
    fs::create_directories("/var/lib/btrfs-backup/public/profiles");
    fs::create_directories(answers.profile.target.mount_point);

    save_tree(answers.profile, "/etc/btrfs-backup", "/etc/udev/rules.d", "/var/lib/btrfs-backup/public/profiles");
    fs::copy_file(output_dir / "systemd" / "btrfs-backup.service", "/etc/systemd/system/btrfs-backup.service", fs::copy_options::overwrite_existing);
    fs::copy_file(output_dir / "systemd" / "btrfs-backup@.service", "/etc/systemd/system/btrfs-backup@.service", fs::copy_options::overwrite_existing);
    fs::copy_file(output_dir / "udev" / "99-btrfs-backup.rules", "/etc/udev/rules.d/99-btrfs-backup.rules", fs::copy_options::overwrite_existing);

    run_command({"systemctl", "disable", "btrfs-backup.service"});
    run_capture({"systemctl", "daemon-reload"});
    run_capture({"udevadm", "control", "--reload-rules"});
    validate_active_installation(answers.profile.id);
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

    WizardAnswers answers = collect_answers(input, output);
    fs::path output_dir = options.output_dir.empty() ? wizard::default_output_dir() : options.output_dir;
    output_dir = fs::absolute(output_dir).lexically_normal();
    render_wizard_tree(answers, output_dir);

    if (options.action == ProfileWizardAction::apply) {
        apply_rendered_tree(answers, output_dir);
        output << "Installed active configuration for profile " << answers.profile.id << "\n";
    } else {
        output << "Rendered and validated files in:\n  " << output_dir << "\n";
        output << "\nReview them, merge the fstab/crypttab fragments manually, or rerun with --apply as root.\n";
    }
    return 0;
}

} // namespace btrfsbackup
