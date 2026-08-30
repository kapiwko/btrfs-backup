// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace btrfsbackup {
class CancellationToken;
}

namespace btrfsbackup::backup {

inline constexpr std::chrono::seconds default_command_timeout{30};
inline constexpr std::size_t default_command_max_output_bytes = 1024 * 1024;

struct CommandResult {
    int exit_code = 0;
    std::string output;
    bool cancelled = false;
    bool timed_out = false;
};

enum class CommandEnvironmentProfile {
    Standard,
    Hook,
    SystemdControl,
};

struct ControlledCommandOptions {
    CancellationToken* cancellation = nullptr;
    std::chrono::milliseconds timeout{300000};
    std::size_t max_output_bytes = 64 * 1024;
    std::chrono::milliseconds terminate_grace_period{5000};
    std::chrono::milliseconds kill_reap_period{5000};
    std::vector<int> inherited_fds;
    CommandEnvironmentProfile environment_profile = CommandEnvironmentProfile::Standard;
    std::map<std::string, std::string> environment;
};

} // namespace btrfsbackup::backup
