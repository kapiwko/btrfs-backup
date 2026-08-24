#include <btrfsbackup/backup_tool.hpp>

#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <btrfsbackup/command/runner_command.hpp>
#include <btrfsbackup/command/target_command.hpp>
#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/profile_loader.hpp>
#include <btrfsbackup/transfer_pipeline.hpp>

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

namespace btrfsbackup {

class TerminationSignalMonitor::Impl {
public:
    explicit Impl(CancellationToken& cancellation)
        : cancellation_(cancellation) {
        sigemptyset(&signals_);
        sigaddset(&signals_, SIGINT);
        sigaddset(&signals_, SIGTERM);

        int mask_error = pthread_sigmask(SIG_BLOCK, &signals_, &previous_mask_);
        if (mask_error != 0) {
            throw ValidationError(std::string("cannot block termination signals: ") + std::strerror(mask_error));
        }
        mask_installed_ = true;

        signal_fd_ = signalfd(-1, &signals_, SFD_CLOEXEC | SFD_NONBLOCK);
        if (signal_fd_ < 0) {
            int signal_fd_error = errno;
            cleanup();
            throw ValidationError(std::string("cannot create termination signal fd: ") + std::strerror(signal_fd_error));
        }
        stop_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (stop_fd_ < 0) {
            int stop_fd_error = errno;
            cleanup();
            throw ValidationError(std::string("cannot create signal monitor stop fd: ") + std::strerror(stop_fd_error));
        }

        try {
            worker_ = std::thread([this] { run(); });
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~Impl() {
        std::uint64_t value = 1;
        ssize_t ignored = write(stop_fd_, &value, sizeof(value));
        (void)ignored;
        if (worker_.joinable()) {
            worker_.join();
        }
        cleanup();
    }

private:
    void run() {
        pollfd fds[2]{
            {.fd = signal_fd_, .events = POLLIN, .revents = 0},
            {.fd = stop_fd_, .events = POLLIN, .revents = 0},
        };
        while (true) {
            int ready = poll(fds, 2, -1);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                cancellation_.request_cancel();
                return;
            }
            if ((fds[1].revents & POLLIN) != 0) {
                return;
            }
            if ((fds[0].revents & POLLIN) == 0) {
                continue;
            }

            signalfd_siginfo signal_info {};
            while (read(signal_fd_, &signal_info, sizeof(signal_info)) == sizeof(signal_info)) {
                if (signal_info.ssi_signo == SIGINT || signal_info.ssi_signo == SIGTERM) {
                    cancellation_.request_cancel();
                }
            }
        }
    }

    void cleanup() {
        if (stop_fd_ >= 0) {
            close(stop_fd_);
            stop_fd_ = -1;
        }
        if (signal_fd_ >= 0) {
            close(signal_fd_);
            signal_fd_ = -1;
        }
        if (mask_installed_) {
            pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
            mask_installed_ = false;
        }
    }

    CancellationToken& cancellation_;
    sigset_t signals_ {};
    sigset_t previous_mask_ {};
    bool mask_installed_ = false;
    int signal_fd_ = -1;
    int stop_fd_ = -1;
    std::thread worker_;
};

TerminationSignalMonitor::TerminationSignalMonitor(CancellationToken& cancellation)
    : impl_(std::make_unique<Impl>(cancellation)) {
}

TerminationSignalMonitor::~TerminationSignalMonitor() = default;

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
              return command::runner(profile_config_dir, runner_args, runner_output, nullptr, cancellation);
          };
    auto target = services != nullptr && services->target
        ? services->target
        : [&](const std::vector<std::string>& target_args, std::ostream& target_output) {
              return command::target(profile_config_dir, target_args, target_output);
          };
    auto load_profile = services != nullptr && services->load_profile
        ? services->load_profile
        : [&](const std::string& profile_id) {
              return load_profile_by_id(profile_config_dir, profile_id);
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

    Profile profile = load_profile(options.profile_id);
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
    } catch (const ValidationError& exc) {
        fail(exc.what());
    } catch (const std::exception& exc) {
        fail(exc.what());
    }
}

} // namespace btrfsbackup
