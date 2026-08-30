// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <sys/types.h>

#include <map>
#include <string>
#include <vector>

namespace btrfsbackup::platform::linux::process {

struct ProcessSpawnOptions {
    int stdin_fd = -1;
    int stdout_fd = -1;
    int stderr_fd = -1;
    bool create_process_group = false;
    std::vector<int> inherited_fds;
    std::map<std::string, std::string> environment;
};

struct ProcessSpawnResult {
    pid_t pid = -1;
    int error = 0;

    bool started() const {
        return pid > 0 && error == 0;
    }
};

ProcessSpawnResult spawn_program(
    const std::vector<std::string>& argv,
    const ProcessSpawnOptions& options = {}
);

} // namespace btrfsbackup::platform::linux::process
