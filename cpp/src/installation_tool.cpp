#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <btrfsbackup/installation_tool.hpp>
#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/process.hpp>
#include <btrfsbackup/profile.hpp>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backupctl: " << message << '\n';
    std::exit(code);
}

std::string arg_value(std::size_t& index, const std::vector<std::string>& args, const std::string& option) {
    if (index + 1 >= args.size()) {
        fail(option + " requires a value");
    }
    return args[++index];
}

std::string fstab_escape(const std::string& value) {
    std::string escaped;
    for (char ch : value) {
        switch (ch) {
            case '\\': escaped += "\\134"; break;
            case '\t': escaped += "\\011"; break;
            case ' ': escaped += "\\040"; break;
            case '#': escaped += "\\043"; break;
            default: escaped += ch; break;
        }
    }
    return escaped;
}

bool contains_unresolved_placeholder(const fs::path& root) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            throw btrfsbackup::ValidationError("cannot scan rendered tree: " + root.string());
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        std::ifstream stream(it->path());
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find("{{") != std::string::npos && line.find("}}") != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

bool allowed_systemd_verify_failure(const std::string& output) {
    bool saw_output = false;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        saw_output = true;
        if (line != "Failed to turn off SO_PASSRIGHTS on user lookup socket, ignoring: Operation not permitted" &&
            line != "Failed to enable SO_PASSCRED on handoff timestamp socket: Operation not permitted") {
            return false;
        }
    }
    return saw_output;
}

void run_checked(const std::vector<std::string>& argv, bool allow_systemd_warnings = false) {
    btrfsbackup::CommandResult result = btrfsbackup::run_command(argv);
    if (result.exit_code == 0) {
        if (!result.output.empty()) {
            std::cerr << result.output;
        }
        return;
    }
    if (allow_systemd_warnings && allowed_systemd_verify_failure(result.output)) {
        std::cerr << result.output;
        return;
    }
    if (!result.output.empty()) {
        std::cerr << result.output;
    }
    throw btrfsbackup::ValidationError("command failed: " + argv.front());
}

std::string render_fstab_fragment(const btrfsbackup::Profile& profile) {
    std::string cryptsetup_unit = btrfsbackup::run_capture({
        "systemd-escape",
        "--template=systemd-cryptsetup@.service",
        profile.target.mapper_name
    });
    return
        "# Merge this single line into /etc/fstab.\n"
        "# noauto prevents mounting at boot. The profile service starts this mount unit only after the matching LUKS device appears.\n"
        "\n"
        "/dev/mapper/" + fstab_escape(profile.target.mapper_name) + "  " +
        fstab_escape(profile.target.mount_point) +
        "  btrfs  noauto,nofail,noatime,compress=zstd,x-systemd.requires=" +
        cryptsetup_unit +
        ",x-systemd.device-timeout=30s,x-systemd.mount-timeout=60s  0  0\n";
}

std::string render_crypttab_fragment(const btrfsbackup::Profile& profile, const std::string& keyfile) {
    return
        "# Merge this single line into /etc/crypttab.\n"
        "# Format: <name> <device> <password> <options>\n"
        "\n" +
        profile.target.mapper_name + "  UUID=" + profile.target.luks_uuid + "  " +
        fstab_escape(keyfile) +
        "  luks,noauto,nofail,x-systemd.device-timeout=30s\n";
}

std::string render_backup_service(const btrfsbackup::Profile& profile, const std::string& backup_script, const std::string& eject_script) {
    return
        "[Unit]\n"
        "Description=Verified Btrfs backup to an encrypted removable target\n"
        "Documentation=file:/usr/share/doc/btrfs-backup/README.md\n"
        "ConditionPathExists=/etc/btrfs-backup\n"
        "After=local-fs.target systemd-udevd.service\n"
        "StartLimitIntervalSec=5min\n"
        "StartLimitBurst=3\n"
        "\n"
        "[Service]\n"
        "Type=oneshot\n"
        "ExecStart=" + backup_script + " --profile " + profile.id + "\n"
        "ExecStopPost=" + eject_script + " --from-service --profile " + profile.id + "\n"
        "User=root\n"
        "Group=root\n"
        "UMask=0077\n"
        "RuntimeDirectory=btrfs-backup\n"
        "RuntimeDirectoryMode=0755\n"
        "StateDirectory=btrfs-backup\n"
        "StateDirectoryMode=0755\n"
        "Nice=10\n"
        "IOSchedulingClass=best-effort\n"
        "IOSchedulingPriority=7\n"
        "TimeoutStartSec=infinity\n"
        "TimeoutStopSec=infinity\n"
        "KillSignal=SIGINT\n"
        "SyslogIdentifier=btrfs-backup\n";
}

std::string render_profile_service(const std::string& backup_script, const std::string& eject_script) {
    return
        "[Unit]\n"
        "Description=Verified Btrfs backup profile %i to an encrypted removable target\n"
        "Documentation=file:/usr/share/doc/btrfs-backup/README.md\n"
        "ConditionPathExists=/etc/btrfs-backup\n"
        "After=local-fs.target systemd-udevd.service\n"
        "StartLimitIntervalSec=5min\n"
        "StartLimitBurst=3\n"
        "\n"
        "[Service]\n"
        "Type=oneshot\n"
        "ExecStart=" + backup_script + " --profile %i\n"
        "ExecStopPost=" + eject_script + " --from-service --profile %i\n"
        "User=root\n"
        "Group=root\n"
        "UMask=0077\n"
        "RuntimeDirectory=btrfs-backup\n"
        "RuntimeDirectoryMode=0755\n"
        "StateDirectory=btrfs-backup\n"
        "StateDirectoryMode=0755\n"
        "Nice=10\n"
        "IOSchedulingClass=best-effort\n"
        "IOSchedulingPriority=7\n"
        "TimeoutStartSec=infinity\n"
        "TimeoutStopSec=infinity\n"
        "KillSignal=SIGINT\n"
        "SyslogIdentifier=btrfs-backup\n";
}

void validate_rendered_installation(const fs::path& root) {
    fs::path profile_json = root / "config" / "profile.json";
    fs::path service_file = root / "systemd" / "btrfs-backup.service";
    fs::path profile_service_file = root / "systemd" / "btrfs-backup@.service";
    fs::path udev_file = root / "udev" / "99-btrfs-backup.rules";

    if (!fs::is_regular_file(profile_json)) fail("missing rendered canonical profile JSON: " + profile_json.string());
    if (!fs::is_regular_file(service_file)) fail("missing rendered systemd unit: " + service_file.string());
    if (!fs::is_regular_file(profile_service_file)) fail("missing rendered systemd template unit: " + profile_service_file.string());
    if (!fs::is_regular_file(udev_file)) fail("missing rendered udev rule: " + udev_file.string());
    if (contains_unresolved_placeholder(root)) {
        fail("unresolved placeholders remain in rendered files");
    }

    btrfsbackup::profile_from_json(btrfsbackup::load_json_file(profile_json));
    run_checked({"systemd-analyze", "verify", service_file.string(), profile_service_file.string()}, true);
    run_checked({"udevadm", "verify", udev_file.string()});
    std::cerr << "Rendered configuration passed syntax, systemd, and udev validation: " << root << '\n';
}

void validate_active_installation(const std::string& profile_id) {
    if (geteuid() != 0) {
        fail("active installation validation must be run as root", 1);
    }
    fs::path profile_json = fs::path("/etc/btrfs-backup/profiles") / profile_id / "profile.json";
    fs::path service_file = "/etc/systemd/system/btrfs-backup.service";
    fs::path profile_service_file = "/etc/systemd/system/btrfs-backup@.service";
    fs::path udev_file = "/etc/udev/rules.d/99-btrfs-backup.rules";

    if (!fs::is_regular_file(profile_json)) fail("missing profile JSON: " + profile_json.string());
    if (!fs::is_regular_file(service_file)) fail("missing " + service_file.string());
    if (!fs::is_regular_file(udev_file)) fail("missing " + udev_file.string());

    btrfsbackup::Profile profile = btrfsbackup::profile_from_json(btrfsbackup::load_json_file(profile_json));
    std::vector<std::string> verify_units = {"systemd-analyze", "verify", service_file.string()};
    if (fs::is_regular_file(profile_service_file)) {
        verify_units.push_back(profile_service_file.string());
    }
    run_checked(verify_units, true);
    run_checked({"udevadm", "verify", udev_file.string()});

    std::string mount_unit = btrfsbackup::run_capture({"systemd-escape", "-p", "--suffix=mount", profile.target.mount_point});
    fs::path old_dropin = fs::path("/etc/systemd/system") / (mount_unit + ".d") / "backup.conf";
    if (fs::is_regular_file(old_dropin)) {
        std::ifstream stream(old_dropin);
        std::string content{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        if ((content.find("Wants=") != std::string::npos || content.find("After=") != std::string::npos) &&
            content.find("btrfs-backup.service") != std::string::npos) {
            fail("obsolete cyclic mount drop-in still exists: " + old_dropin.string());
        }
    }

    std::cerr << "Active static configuration is valid. Run 'sudo btrfs-backup --validate' with the target connected for runtime validation.\n";
}

} // namespace

namespace btrfsbackup {

int command_render_installation(const std::vector<std::string>& args) {
    fs::path file;
    fs::path output_dir;
    std::string backup_script = "/usr/lib/btrfs-backup/btrfs-backup.sh";
    std::string eject_script = "/usr/lib/btrfs-backup/btrfs-backup-eject.sh";
    std::string keyfile = "none";
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--file") {
            file = arg_value(i, args, arg);
        } else if (arg == "--output-dir") {
            output_dir = arg_value(i, args, arg);
        } else if (arg == "--backup-script") {
            backup_script = arg_value(i, args, arg);
        } else if (arg == "--eject-script") {
            eject_script = arg_value(i, args, arg);
        } else if (arg == "--keyfile") {
            keyfile = arg_value(i, args, arg);
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: btrfs-backupctl installation render --file PATH --output-dir PATH [--backup-script PATH] [--eject-script PATH] [--keyfile PATH]\n";
            return 0;
        } else {
            fail("unknown installation render option: " + arg);
        }
    }
    if (file.empty()) fail("installation render requires --file");
    if (output_dir.empty()) fail("installation render requires --output-dir");

    Profile profile = profile_from_json(load_json_file(file));
    fs::create_directories(output_dir / "config");
    fs::create_directories(output_dir / "systemd");
    atomic_write(output_dir / "config" / "fstab.fragment", render_fstab_fragment(profile), 0644);
    atomic_write(output_dir / "config" / "crypttab.fragment", render_crypttab_fragment(profile, keyfile), 0644);
    atomic_write(output_dir / "systemd" / "btrfs-backup.service", render_backup_service(profile, backup_script, eject_script), 0644);
    atomic_write(output_dir / "systemd" / "btrfs-backup@.service", render_profile_service(backup_script, eject_script), 0644);
    return 0;
}

int command_validate_installation(const std::vector<std::string>& args) {
    fs::path rendered_root;
    bool active = false;
    std::string profile_id = "default";
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--rendered-root") {
            rendered_root = arg_value(i, args, arg);
        } else if (arg == "--active") {
            active = true;
        } else if (arg == "--profile") {
            profile_id = arg_value(i, args, arg);
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: btrfs-backupctl installation validate (--rendered-root PATH | --active [--profile ID])\n";
            return 0;
        } else {
            fail("unknown installation validate option: " + arg);
        }
    }
    if (active == !rendered_root.empty()) {
        fail("installation validate requires exactly one of --rendered-root or --active");
    }
    if (active) {
        validate_active_installation(profile_id);
    } else {
        validate_rendered_installation(rendered_root);
    }
    return 0;
}

void installation_usage() {
    std::cout << "Usage: btrfs-backupctl installation COMMAND\n"
              << "\nCommands:\n"
              << "  render --file PATH --output-dir PATH\n"
              << "  validate (--rendered-root PATH | --active [--profile ID])\n";
}

int command_installation(const std::vector<std::string>& args) {
    if (args.empty()) {
        installation_usage();
        return 2;
    }
    std::string command = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());
    if (command == "render") {
        return command_render_installation(rest);
    }
    if (command == "validate") {
        return command_validate_installation(rest);
    }
    if (command == "-h" || command == "--help") {
        installation_usage();
        return 0;
    }
    fail("unknown installation command: " + command);
}

} // namespace btrfsbackup
