// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/CtlTool.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <print>
#include <string>
#include <vector>

#include <platform/linux/config/ApplicationConfig.hpp>
#include <cli/BackupTool.hpp>
#include <cli/InstallationCommand.hpp>
#include <cli/profile/ProfileCommand.hpp>
#include <cli/repository/RepositoryCommand.hpp>
#include <cli/runner/RunnerCommand.hpp>
#include <cli/runner/RunnerOptions.hpp>
#include <cli/restore/RestoreCommand.hpp>
#include <cli/status/StatusCommand.hpp>
#include <cli/target/TargetCommand.hpp>
#include <config/ConfigurationIdentity.hpp>
#include <core/Errors.hpp>
#include <core/Cancellation.hpp>
#include <restore/RestoreError.hpp>
#include <platform/linux/systemd/LinuxSystemConfigurationActivator.hpp>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::println(std::cerr, "btrfs-backupctl: {}", message);
    std::exit(code);
}

std::string arg_value(int& index, int argc, char** argv, const std::string& option) {
    if (index + 1 >= argc) {
        fail(option + " requires a value");
    }
    return argv[++index];
}

void usage() {
    std::print(
        "Usage: btrfs-backupctl [options] COMMAND\n"
        "\nOptions:\n"
        "  --status-root PATH   Override status root (default: /run/btrfs-backup/profiles).\n"
        "  --history-root PATH  Override history root (default: /var/lib/btrfs-backup/history).\n"
        "  --profile-dir PATH   Override profile config root (default: /etc/btrfs-backup).\n"
        "\nCommands:\n"
        "  profile COMMAND\n"
        "  repository COMMAND\n"
        "  status COMMAND\n"
        "  installation COMMAND\n"
        "  runner COMMAND\n"
        "  restore COMMAND\n"
        "  target COMMAND\n"
        "  -h, --help\n"
    );
}

} // namespace

namespace btrfsbackup::cli {

int ctl_tool_main(int argc, char** argv) {
    bool status_root_overridden = std::getenv("BTRFS_BACKUP_STATUS_ROOT") != nullptr;
    bool history_root_overridden = std::getenv("BTRFS_BACKUP_HISTORY_ROOT") != nullptr;
    fs::path status_root = status_root_overridden ? std::getenv("BTRFS_BACKUP_STATUS_ROOT") : "/run/btrfs-backup/profiles";
    fs::path history_root = history_root_overridden ? std::getenv("BTRFS_BACKUP_HISTORY_ROOT") : "/var/lib/btrfs-backup/history";
    fs::path profile_config_dir = std::getenv("BTRFS_BACKUP_PROFILE_CONFIG_DIR") ? std::getenv("BTRFS_BACKUP_PROFILE_CONFIG_DIR") : "/etc/btrfs-backup";
    std::vector<std::string> rest;

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--status-root") {
                status_root = arg_value(i, argc, argv, arg);
                status_root_overridden = true;
            } else if (arg == "--history-root") {
                history_root = arg_value(i, argc, argv, arg);
                history_root_overridden = true;
            } else if (arg == "--profile-dir") {
                profile_config_dir = arg_value(i, argc, argv, arg);
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                for (; i < argc; ++i) {
                    rest.emplace_back(argv[i]);
                }
                break;
            }
        }

        if (rest.empty()) {
            usage();
            return 2;
        }
        std::string command = rest[0];
        std::vector<std::string> args(rest.begin() + 1, rest.end());

        if (command == "profile") {
            btrfsbackup::platform::linux::systemd::LinuxSystemConfigurationActivator activator;
            return profile::profile(args, profile_config_dir, activator);
        } else if (command == "repository") {
            return repository::repository(profile_config_dir, args, std::cout);
        } else if (command == "status") {
            bool help_requested = std::ranges::contains(args, "-h") || std::ranges::contains(args, "--help");
            if (!help_requested) {
                btrfsbackup::config::ApplicationConfig application_config = btrfsbackup::platform::linux::config::load_application_config(profile_config_dir);
                if (!status_root_overridden)
                    status_root = application_config.paths().status_root;
                if (!history_root_overridden)
                    history_root = application_config.paths().history_root;
            }
            return status::status(status_root, history_root, args);
        } else if (command == "installation") {
            return installation(args);
        } else if (command == "runner") {
            CancellationToken cancellation;
            TerminationSignalMonitor termination_signals(cancellation);
            return runner::runner(profile_config_dir, args, std::cout, cancellation);
        } else if (command == "restore") {
            CancellationToken cancellation;
            TerminationSignalMonitor termination_signals(cancellation);
            return restore::restore(args, std::cout, cancellation);
        } else if (command == "target") {
            return target::target(profile_config_dir, args, std::cout);
        } else if (command == "-h" || command == "--help") {
            usage();
        } else {
            fail("unknown command: " + command);
        }
    } catch (const runner::RunnerOptionsError& exc) {
        std::println(std::cerr, "btrfs-backupctl runner: {}", exc.what());
        return 2;
    } catch (const CodedValidationError& exc) {
        fail(
            exc.what(),
            exc.error_code == ErrorCode::ConfigurationChanged
                ? btrfsbackup::config::configuration_changed_exit_code
                : 2
        );
    } catch (const ValidationError& exc) {
        fail(exc.what());
    } catch (const btrfsbackup::restore::RestoreError& exc) {
        fail(btrfsbackup::restore::restore_error_code_name(exc.code()) + ": " + exc.what());
    } catch (const std::exception& exc) {
        fail(exc.what());
    }
    return 0;
}

} // namespace btrfsbackup::cli
