#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <btrfsbackup/command/installation_command.hpp>
#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/installation_render.hpp>
#include <btrfsbackup/installation_validate.hpp>
#include <btrfsbackup/json_io.hpp>
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

} // namespace

namespace btrfsbackup::command {

int render_installation(const std::vector<std::string>& args) {
    fs::path file;
    fs::path output_dir;
    btrfsbackup::InstallationRenderOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--file") {
            file = arg_value(i, args, arg);
        } else if (arg == "--output-dir") {
            output_dir = arg_value(i, args, arg);
        } else if (arg == "--backup-script") {
            options.backup_script = arg_value(i, args, arg);
        } else if (arg == "--eject-script") {
            options.eject_script = arg_value(i, args, arg);
        } else if (arg == "--keyfile") {
            options.keyfile = arg_value(i, args, arg);
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
    render_installation_files(profile, output_dir, options);
    return 0;
}

int validate_installation(const std::vector<std::string>& args) {
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
        if (geteuid() != 0) {
            fail("active installation validation must be run as root", 1);
        }
        btrfsbackup::validate_active_installation(profile_id);
    } else {
        btrfsbackup::validate_rendered_installation(rendered_root);
    }
    return 0;
}

void installation_usage() {
    std::cout << "Usage: btrfs-backupctl installation COMMAND\n"
              << "\nCommands:\n"
              << "  render --file PATH --output-dir PATH\n"
              << "  validate (--rendered-root PATH | --active [--profile ID])\n";
}

int installation(const std::vector<std::string>& args) {
    if (args.empty()) {
        installation_usage();
        return 2;
    }
    std::string command = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());
    if (command == "render") {
        return render_installation(rest);
    }
    if (command == "validate") {
        return validate_installation(rest);
    }
    if (command == "-h" || command == "--help") {
        installation_usage();
        return 0;
    }
    fail("unknown installation command: " + command);
}

} // namespace btrfsbackup::command
