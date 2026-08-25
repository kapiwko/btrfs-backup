// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <config/identifiers.hpp>

namespace btrfsbackup {

inline constexpr std::chrono::seconds default_command_timeout{30};
inline constexpr std::size_t default_command_max_output_bytes = 1024 * 1024;

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
    std::vector<int> inherited_fds;
    std::optional<ProfileId> profile_id;
    std::optional<SourceId> source_id;
};

CommandResult run_command(const std::vector<std::string>& argv);
CommandResult run_controlled_command(
    const std::vector<std::string>& argv,
    const ControlledCommandOptions& options
);
std::string run_capture(const std::vector<std::string>& argv);

} // namespace btrfsbackup
