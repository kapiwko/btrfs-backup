#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace btrfsbackup {

struct CommandResult {
    int exit_code = 0;
    std::string output;
    bool cancelled = false;
    bool timed_out = false;
};

struct ControlledCommandOptions {
    int cancellation_fd = -1;
    std::chrono::milliseconds timeout{300000};
    std::size_t max_output_bytes = 64 * 1024;
    std::chrono::milliseconds terminate_grace_period{5000};
    std::chrono::milliseconds kill_reap_period{5000};
};

CommandResult run_command(const std::vector<std::string>& argv);
CommandResult run_controlled_command(
    const std::vector<std::string>& argv,
    const ControlledCommandOptions& options
);
std::string run_capture(const std::vector<std::string>& argv);

} // namespace btrfsbackup
