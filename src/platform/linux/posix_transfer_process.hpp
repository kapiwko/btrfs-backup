// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <backup/transfer/transfer_plan.hpp>
#include <backup/transfer/transfer_result.hpp>
#include <platform/linux/process_spawn.hpp>

namespace btrfsbackup::platform::linux {

ProcessSpawnResult spawn_posix_transfer_process(
    const std::vector<std::string>& argv,
    int stdin_fd,
    int stdout_fd,
    int stderr_fd,
    const std::vector<std::shared_ptr<btrfsbackup::backup::transfer::ITransferResource>>& resources
);

bool reap_posix_transfer_process(pid_t pid, btrfsbackup::backup::transfer::TransferSideResult& result);

} // namespace btrfsbackup::platform::linux
