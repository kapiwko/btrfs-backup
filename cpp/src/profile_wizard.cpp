#include <btrfsbackup/profile_wizard.hpp>

#include <libmount/libmount.h>
#include <libudev.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
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

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

struct DeviceCandidate {
    std::string path;
    std::string type;
    std::string size;
    std::string transport;
    std::string model;
    std::string serial;
    std::string uuid;
    bool removable = false;
};

struct WizardAnswers {
    Profile profile;
    std::string keyfile = "none";
};

std::string trim(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool bool_value(const std::string& value) {
    std::string normalized = lower(trim(value));
    if (normalized == "1" || normalized == "yes" || normalized == "true" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "no" || normalized == "false" || normalized == "off") {
        return false;
    }
    throw ValidationError("enter true or false");
}

long long uint_value(const std::string& value) {
    std::string normalized = trim(value);
    if (normalized.empty() || !std::all_of(normalized.begin(), normalized.end(), [](unsigned char c) { return std::isdigit(c); })) {
        throw ValidationError("enter a non-negative integer");
    }
    return std::stoll(normalized);
}

std::string prompt_value(std::istream& input, std::ostream& output, const std::string& label, const std::string& default_value) {
    output << label << " [" << default_value << "]: " << std::flush;
    std::string line;
    if (!std::getline(input, line)) {
        throw ValidationError("input ended while reading: " + label);
    }
    line = trim(line);
    return line.empty() ? default_value : line;
}

bool prompt_bool(std::istream& input, std::ostream& output, const std::string& label, bool default_value) {
    std::string default_text = default_value ? "true" : "false";
    while (true) {
        try {
            return bool_value(prompt_value(input, output, label, default_text));
        } catch (const ValidationError& error) {
            output << error.what() << '\n';
        }
    }
}

long long prompt_uint(std::istream& input, std::ostream& output, const std::string& label, long long default_value) {
    while (true) {
        try {
            return uint_value(prompt_value(input, output, label, std::to_string(default_value)));
        } catch (const ValidationError& error) {
            output << error.what() << '\n';
        }
    }
}

std::string c_string(const char* value) {
    return value == nullptr ? "" : value;
}

std::string udev_property(udev_device* device, const char* key) {
    return c_string(udev_device_get_property_value(device, key));
}

std::string udev_sysattr(udev_device* device, const char* key) {
    return c_string(udev_device_get_sysattr_value(device, key));
}

std::vector<DeviceCandidate> detect_luks_devices() {
    std::unique_ptr<udev, decltype(&udev_unref)> udev_context(udev_new(), udev_unref);
    if (!udev_context) {
        throw ValidationError("could not initialize libudev");
    }
    std::unique_ptr<udev_enumerate, decltype(&udev_enumerate_unref)> enumerate(
        udev_enumerate_new(udev_context.get()),
        udev_enumerate_unref
    );
    if (!enumerate) {
        throw ValidationError("could not enumerate block devices");
    }
    udev_enumerate_add_match_subsystem(enumerate.get(), "block");
    udev_enumerate_scan_devices(enumerate.get());

    std::vector<DeviceCandidate> devices;
    udev_list_entry* entries = udev_enumerate_get_list_entry(enumerate.get());
    udev_list_entry* entry = nullptr;
    udev_list_entry_foreach(entry, entries) {
        const char* syspath = udev_list_entry_get_name(entry);
        std::unique_ptr<udev_device, decltype(&udev_device_unref)> device(
            udev_device_new_from_syspath(udev_context.get(), syspath),
            udev_device_unref
        );
        if (!device) {
            continue;
        }
        if (udev_property(device.get(), "ID_FS_TYPE") != "crypto_LUKS") {
            continue;
        }
        std::string devtype = udev_property(device.get(), "DEVTYPE");
        if (devtype != "partition" && devtype != "disk") {
            continue;
        }
        std::string uuid = udev_property(device.get(), "ID_FS_UUID");
        if (uuid.empty()) {
            continue;
        }
        std::string devnode = c_string(udev_device_get_devnode(device.get()));
        if (devnode.empty()) {
            continue;
        }
        std::string transport = udev_property(device.get(), "ID_BUS");
        devices.push_back({
            devnode,
            devtype == "partition" ? "part" : "disk",
            "",
            transport,
            udev_property(device.get(), "ID_MODEL"),
            udev_property(device.get(), "ID_SERIAL_SHORT"),
            uuid,
            trim(udev_sysattr(device.get(), "removable")) == "1" || transport == "usb"
        });
    }
    std::sort(devices.begin(), devices.end(), [](const DeviceCandidate& left, const DeviceCandidate& right) {
        return left.path < right.path;
    });
    return devices;
}

std::string udev_property_for_device(const std::string& device_path, const char* key) {
    struct stat stat_buffer {};
    if (stat(device_path.c_str(), &stat_buffer) != 0) {
        return "";
    }
    std::unique_ptr<udev, decltype(&udev_unref)> udev_context(udev_new(), udev_unref);
    if (!udev_context) {
        return "";
    }
    std::unique_ptr<udev_device, decltype(&udev_device_unref)> device(
        udev_device_new_from_devnum(udev_context.get(), 'b', stat_buffer.st_rdev),
        udev_device_unref
    );
    if (!device) {
        return "";
    }
    return udev_property(device.get(), key);
}

std::string best_device_reference(const DeviceCandidate& device) {
    fs::path uuid_path = fs::path("/dev/disk/by-uuid") / device.uuid;
    if (fs::exists(uuid_path)) {
        return uuid_path.string();
    }
    return device.path;
}

std::string source_name_from_path(const std::string& path) {
    if (path == "/") {
        return "root";
    }
    std::string name = fs::path(path).filename().string();
    for (char& ch : name) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '.' && ch != '_' && ch != '-') {
            ch = '-';
        }
    }
    if (name.empty() || !std::isalnum(static_cast<unsigned char>(name.front()))) {
        name = "source-" + name;
    }
    return name;
}

std::vector<std::string> detect_btrfs_sources() {
    std::unique_ptr<libmnt_table, decltype(&mnt_unref_table)> table(mnt_new_table(), mnt_unref_table);
    if (!table || mnt_table_parse_file(table.get(), "/proc/self/mountinfo") != 0) {
        throw ValidationError("could not read mount table");
    }
    std::unique_ptr<libmnt_iter, decltype(&mnt_free_iter)> iter(mnt_new_iter(MNT_ITER_FORWARD), mnt_free_iter);
    if (!iter) {
        throw ValidationError("could not iterate mount table");
    }
    std::set<std::string> unique;
    libmnt_fs* mount = nullptr;
    while (mnt_table_next_fs(table.get(), iter.get(), &mount) == 0) {
        if (std::string(c_string(mnt_fs_get_fstype(mount))) == "btrfs") {
            std::string target = c_string(mnt_fs_get_target(mount));
            if (!target.empty()) {
                unique.insert(target);
            }
        }
    }
    return {unique.begin(), unique.end()};
}

std::vector<std::string> select_sources(std::istream& input, std::ostream& output) {
    std::vector<std::string> candidates = detect_btrfs_sources();
    if (candidates.empty()) {
        throw ValidationError("no mounted Btrfs source subvolumes were detected");
    }
    output << "\nDetected mounted Btrfs sources:\n";
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        output << " " << (i + 1) << ") " << candidates[i] << '\n';
    }

    std::vector<std::string> defaults;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i] == "/" || candidates[i] == "/home") {
            defaults.push_back(std::to_string(i + 1));
        }
    }
    std::string default_selection = defaults.empty() ? "1" : defaults.front();
    for (std::size_t i = 1; i < defaults.size(); ++i) {
        default_selection += "," + defaults[i];
    }

    std::string selection = prompt_value(input, output, "Select one or more sources, comma-separated, or 'a' for all", default_selection);
    if (selection == "a" || selection == "A") {
        return candidates;
    }

    std::vector<std::string> selected;
    std::set<std::size_t> seen;
    std::istringstream tokens(selection);
    std::string token;
    while (std::getline(tokens, token, ',')) {
        token = trim(token);
        if (token.empty() || !std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c); })) {
            throw ValidationError("invalid source selection: " + token);
        }
        std::size_t index = static_cast<std::size_t>(std::stoul(token));
        if (index == 0 || index > candidates.size()) {
            throw ValidationError("source selection out of range: " + token);
        }
        if (seen.insert(index - 1).second) {
            selected.push_back(candidates[index - 1]);
        }
    }
    if (selected.empty()) {
        throw ValidationError("no sources selected");
    }
    return selected;
}

DeviceCandidate select_device(std::istream& input, std::ostream& output) {
    std::vector<DeviceCandidate> devices = detect_luks_devices();
    if (devices.empty()) {
        throw ValidationError("no LUKS block devices were detected");
    }
    output << "Detected LUKS devices:\n";
    for (std::size_t i = 0; i < devices.size(); ++i) {
        output << " " << (i + 1) << ") "
               << (devices[i].removable ? "[removable] " : "")
               << devices[i].path << " | " << (devices[i].size.empty() ? "?" : devices[i].size)
               << " | " << (devices[i].transport.empty() ? "?" : devices[i].transport)
               << " | " << (devices[i].model.empty() ? "?" : devices[i].model)
               << " | UUID=" << devices[i].uuid
               << " | " << (devices[i].serial.empty() ? "no-serial" : devices[i].serial)
               << '\n';
    }
    std::string choice = prompt_value(input, output, "Select backup device", "1");
    if (!std::all_of(choice.begin(), choice.end(), [](unsigned char c) { return std::isdigit(c); })) {
        throw ValidationError("invalid device selection: " + choice);
    }
    std::size_t index = static_cast<std::size_t>(std::stoul(choice));
    if (index == 0 || index > devices.size()) {
        throw ValidationError("device selection out of range: " + choice);
    }
    return devices[index - 1];
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

std::string detect_btrfs_uuid(const std::string& mapper_name) {
    fs::path mapper_path = fs::path("/dev/mapper") / mapper_name;
    if (!fs::exists(mapper_path)) {
        return "";
    }
    return udev_property_for_device(mapper_path.string(), "ID_FS_UUID");
}

Profile build_profile(const WizardAnswers& answers) {
    return profile_from_json(profile_to_json(answers.profile));
}

WizardAnswers collect_answers(std::istream& input, std::ostream& output) {
    DeviceCandidate device = select_device(input, output);

    WizardAnswers answers;
    Profile profile;
    profile.schema_version = 1;
    profile.id = prompt_value(input, output, "Profile identifier", "default");
    validate_identifier(profile.id, "profileId");
    profile.name = prompt_value(input, output, "Profile display name", profile.id);
    profile.enabled = true;

    profile.target.device = best_device_reference(device);
    profile.target.luks_uuid = lower(device.uuid);
    profile.target.partition_uuid = udev_property_for_device(device.path, "ID_PART_ENTRY_UUID");
    profile.target.serial = udev_property_for_device(device.path, "ID_SERIAL_SHORT");
    profile.target.mapper_name = prompt_value(input, output, "LUKS mapper name", "backupdisk");
    validate_identifier(profile.target.mapper_name, "target.mapperName");
    profile.target.mount_point = prompt_value(input, output, "Backup mountpoint", "/mnt/backup");
    profile.target.btrfs_uuid = prompt_value(
        input,
        output,
        "Expected Btrfs UUID inside LUKS (empty disables this additional check)",
        detect_btrfs_uuid(profile.target.mapper_name)
    );

    profile.paths.remote_root = profile.target.mount_point + "/snapshots";
    profile.paths.incoming_root = profile.target.mount_point + "/.incoming";
    profile.paths.sources_dir = "/etc/btrfs-backup/profiles/" + profile.id + "/sources.d";
    profile.paths.state_dir = "/var/lib/btrfs-backup";
    profile.paths.status_root = "/run/btrfs-backup/profiles";
    profile.paths.history_root = "/var/lib/btrfs-backup/history";

    std::vector<std::string> source_paths = select_sources(input, output);
    std::set<std::string> used_names;
    for (const auto& source_path : source_paths) {
        std::string default_name = source_name_from_path(source_path);
        while (used_names.count(default_name) > 0) {
            default_name += "-2";
        }
        ProfileSource source;
        source.id = prompt_value(input, output, "Source name for " + source_path, default_name);
        validate_identifier(source.id, "source.id");
        if (!used_names.insert(source.id).second) {
            throw ValidationError("duplicate source name: " + source.id);
        }
        source.name = source.id;
        source.enabled = true;
        source.subvolume = source_path;
        source.local_snapshot_dir = prompt_value(input, output, "Local snapshot directory for " + source_path, "/.snapshots/btrfs-backup/" + source.id);
        source.remote_subdir = prompt_value(input, output, "Remote subdirectory under the backup snapshots root", source.id);
        profile.sources.push_back(source);
    }

    profile.settings.remote_retention = prompt_uint(input, output, "Remote retention count; 0 means unlimited", 30);
    profile.settings.local_retention = prompt_uint(input, output, "Local retention count; 0 means unlimited", 30);
    for (auto& source : profile.sources) {
        source.remote_retention = profile.settings.remote_retention;
        source.local_retention = profile.settings.local_retention;
    }
    profile.settings.daily_limit = prompt_bool(input, output, "Run at most once per local calendar day", true);
    profile.settings.incremental_required = prompt_bool(input, output, "Fail instead of silently starting a new full chain when remote snapshots exist", true);
    profile.settings.keep_failed_local_snapshot = prompt_bool(input, output, "Keep a new local snapshot after a failed transfer", false);
    profile.settings.auto_eject = prompt_bool(input, output, "Unmount and close LUKS automatically after the service finishes", true);
    profile.settings.minimum_target_free_bytes = prompt_uint(input, output, "Minimum free bytes required on the backup target; 0 disables", 5368709120LL);
    profile.settings.minimum_local_free_bytes = prompt_uint(input, output, "Minimum free bytes required for local snapshots; 0 disables", 1073741824LL);
    answers.keyfile = prompt_value(input, output, "crypttab keyfile path or none", "/root/keys/" + profile.target.mapper_name + ".key");

    profile.notifications.enabled = prompt_bool(input, output, "Enable notifications", true);
    profile.notifications.user = prompt_value(input, output, "Desktop notification user", default_user());
    profile.notifications.method = prompt_value(input, output, "Notification method: auto, desktop, journal, or none", "auto");

    answers.profile = build_profile({profile, answers.keyfile});
    return answers;
}

fs::path default_output_dir() {
    if (geteuid() == 0) {
        return "/etc/btrfs-backup/generated";
    }
    return fs::current_path() / "generated";
}

void assert_safe_output_dir(const fs::path& output_dir) {
    fs::path normalized = fs::absolute(output_dir).lexically_normal();
    static const std::set<std::string> unsafe{"/", "/etc", "/usr", "/var", "/home", "/root"};
    if (unsafe.count(normalized.string()) > 0) {
        throw ValidationError("refusing unsafe output directory: " + normalized.string());
    }
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
    assert_safe_output_dir(output_dir);
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
    fs::path output_dir = options.output_dir.empty() ? default_output_dir() : options.output_dir;
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
