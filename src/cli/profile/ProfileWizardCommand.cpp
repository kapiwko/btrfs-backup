// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/profile/ProfileWizardCommand.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <platform/linux/config/ProfileWizard.hpp>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backupctl profile wizard: " << message << '\n';
    std::exit(code);
}

std::string arg_value(std::size_t& index, const std::vector<std::string>& args, const std::string& option) {
    if (index + 1 >= args.size()) {
        fail(option + " requires a value");
    }
    return args[++index];
}

void usage() {
    std::cout << "Usage: btrfs-backupctl profile wizard [options]\n"
              << "\nActions:\n"
              << "  --render-only         Interactively render configuration files (default)\n"
              << "  --apply               Render and install active files under /etc\n"
              << "  --validate            Validate the active installation\n"
              << "  --validate-dir PATH   Validate a previously rendered directory\n"
              << "\nInput/output:\n"
              << "  --output-dir PATH     Override the rendered output directory\n"
              << "  --profile ID          Profile id for --validate (default: default)\n"
              << "  -h, --help            Show this help\n";
}

} // namespace

namespace btrfsbackup::cli::profile {

int profile_wizard(const std::vector<std::string>& args) {
    btrfsbackup::platform::linux::ProfileWizardOptions options;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--render-only") {
            options.action = btrfsbackup::platform::linux::ProfileWizardAction::render;
        } else if (arg == "--apply") {
            options.action = btrfsbackup::platform::linux::ProfileWizardAction::apply;
        } else if (arg == "--validate") {
            options.action = btrfsbackup::platform::linux::ProfileWizardAction::validate_active;
        } else if (arg == "--validate-dir") {
            options.action = btrfsbackup::platform::linux::ProfileWizardAction::validate_rendered;
            options.validate_dir = arg_value(i, args, arg);
        } else if (arg == "--output-dir") {
            options.output_dir = arg_value(i, args, arg);
        } else if (arg == "--profile") {
            options.profile_id = arg_value(i, args, arg);
        } else if (arg == "--cli" || arg == "--template-dir") {
            if (arg == "--template-dir") {
                (void)arg_value(i, args, arg);
            }
        } else if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else {
            fail("unknown option: " + arg);
        }
    }

    if (options.action == btrfsbackup::platform::linux::ProfileWizardAction::validate_rendered && options.validate_dir.empty()) {
        fail("--validate-dir requires a path");
    }
    return btrfsbackup::platform::linux::run_profile_wizard(options, std::cin, std::cout);
}

} // namespace btrfsbackup::cli::profile
