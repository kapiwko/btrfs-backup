// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/BackupTool.hpp>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <cli/RunnerCommand.hpp>
#include <cli/TargetCommand.hpp>
#include <core/Errors.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>
#include <core/Cancellation.hpp>
#include <platform/linux/TerminationSignalMonitor.hpp>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backup: " << message << '\n';
    std::exit(code);
}

std::string arg_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        throw btrfsbackup::ValidationError(option + " requires a value");
    }
    return args[++index];
}

void usage(std::ostream& output) {
    output << "Usage: btrfs-backup [options]\n"
           << "\nOptions:\n"
           << "  --profile ID   Use /etc/btrfs-backup/profiles/ID/profile.json.\n"
           << "  --force        Run even if a successful backup was already made today.\n"
           << "  --validate     Mount the target and validate configuration without creating snapshots.\n"
           << "  --no-eject     Do not automatically eject after a manual invocation.\n"
           << "  -h, --help     Show this help.\n";
}

struct BackupOptions {
    std::string profile_id = "default";
    bool force = false;
    bool validate_only = false;
    bool no_eject = false;
};

BackupOptions parse_options(const std::vector<std::string>& args) {
    BackupOptions options;
    if (const char* env = std::getenv("BTRFS_BACKUP_PROFILE")) {
        options.profile_id = env;
    }

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args.at(i);
        if (arg == "--profile") {
            options.profile_id = arg_value(args, i, arg);
        } else if (arg == "--force") {
            options.force = true;
        } else if (arg == "--validate") {
            options.validate_only = true;
        } else if (arg == "--no-eject") {
            options.no_eject = true;
        } else {
            throw btrfsbackup::ValidationError("unknown option: " + arg);
        }
    }
    return options;
}

bool default_is_service_invocation() {
    const char* invocation_id = std::getenv("INVOCATION_ID");
    return invocation_id != nullptr && std::string(invocation_id).size() > 0;
}

} // namespace

namespace btrfsbackup::cli {

class TerminationSignalMonitor::Impl {
  public:
    explicit Impl(CancellationToken& cancellation)
        : monitor_([&cancellation] { cancellation.request_cancel(); }) {
    }

  private:
    platform::linux::TerminationSignalMonitor monitor_;
};

TerminationSignalMonitor::TerminationSignalMonitor(CancellationToken& cancellation)
    : impl_(std::make_unique<Impl>(cancellation)) {
}

TerminationSignalMonitor::~TerminationSignalMonitor() noexcept = default;

int backup_tool(
    const fs::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    BackupToolServices* services,
    CancellationToken* cancellation
) {
    if (args.size() == 1 && (args.at(0) == "-h" || args.at(0) == "--help")) {
        usage(output);
        return 0;
    }

    BackupOptions options = parse_options(args);

    auto runner = services != nullptr && services->runner
        ? services->runner
        : [&](const std::vector<std::string>& runner_args, std::ostream& runner_output) {
              if (cancellation != nullptr) {
                  return btrfsbackup::cli::runner(profile_config_dir, runner_args, runner_output, *cancellation);
              }
              return btrfsbackup::cli::runner(profile_config_dir, runner_args, runner_output);
          };
    auto target = services != nullptr && services->target
        ? services->target
        : [&](const std::vector<std::string>& target_args, std::ostream& target_output) {
              return btrfsbackup::cli::target(profile_config_dir, target_args, target_output);
          };
    auto load_profile = services != nullptr && services->load_profile
        ? services->load_profile
        : [&](const std::string& profile_id) {
              return btrfsbackup::platform::linux::load_profile_by_id(profile_config_dir, profile_id);
          };
    auto is_service_invocation = services != nullptr && services->is_service_invocation
        ? services->is_service_invocation
        : default_is_service_invocation;

    std::vector<std::string> runner_args{"execute", "--profile", options.profile_id};
    if (options.force) {
        runner_args.push_back("--force");
    }
    if (options.validate_only) {
        runner_args.push_back("--validate");
    }

    int status = runner(runner_args, output);
    if (status != 0 || options.no_eject || is_service_invocation()) {
        return status;
    }

    btrfsbackup::config::Profile profile = load_profile(options.profile_id);
    if (!profile.settings.auto_eject) {
        return status;
    }

    std::vector<std::string> eject_args{"eject", "--from-runner", "--profile", options.profile_id};
    return target(eject_args, output);
}

int backup_tool_main(int argc, char** argv) {
    fs::path profile_config_dir = std::getenv("BTRFS_BACKUP_PROFILE_CONFIG_DIR")
        ? std::getenv("BTRFS_BACKUP_PROFILE_CONFIG_DIR")
        : "/etc/btrfs-backup";
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    try {
        CancellationToken cancellation;
        TerminationSignalMonitor termination_signals(cancellation);
        return backup_tool(profile_config_dir, args, std::cout, nullptr, &cancellation);
    } catch (const CodedValidationError& exc) {
        fail(
            exc.what(),
            exc.error_code == ErrorCode::ConfigurationChanged
                ? btrfsbackup::config::configuration_changed_exit_code
                : 2
        );
    } catch (const ValidationError& exc) {
        fail(exc.what());
    } catch (const std::exception& exc) {
        fail(exc.what());
    }
}

} // namespace btrfsbackup::cli
