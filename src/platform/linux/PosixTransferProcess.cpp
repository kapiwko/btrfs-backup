// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/PosixTransferProcess.hpp>

#include <sys/wait.h>

#include <cerrno>
#include <cstring>

#include <core/Errors.hpp>
#include <platform/linux/SafeDirectoryRoot.hpp>

namespace btrfsbackup::platform::linux {

namespace {

int status_to_exit_code(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 128;
}

std::vector<int> inherited_fds(const std::vector<std::shared_ptr<btrfsbackup::backup::transfer::ITransferResource>>& resources) {
    std::vector<int> result;
    result.reserve(resources.size());
    for (const std::shared_ptr<btrfsbackup::backup::transfer::ITransferResource>& resource : resources) {
        const auto handle = std::dynamic_pointer_cast<SafeDirectoryHandle>(resource);
        if (!handle) {
            throw ValidationError("unsupported POSIX transfer resource");
        }
        result.push_back(handle->fd());
    }
    return result;
}

} // namespace

ProcessSpawnResult spawn_posix_transfer_process(
    const std::vector<std::string>& argv,
    int stdin_fd,
    int stdout_fd,
    int stderr_fd,
    const std::vector<std::shared_ptr<btrfsbackup::backup::transfer::ITransferResource>>& resources
) {
    if (argv.empty()) {
        throw ValidationError("empty transfer command");
    }
    return spawn_program(argv, {
                                   .stdin_fd = stdin_fd,
                                   .stdout_fd = stdout_fd,
                                   .stderr_fd = stderr_fd,
                                   .create_process_group = true,
                                   .inherited_fds = inherited_fds(resources),
                                   .environment = {},
                               });
}

bool reap_posix_transfer_process(pid_t pid, btrfsbackup::backup::transfer::TransferSideResult& result) {
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, WNOHANG);
    } while (waited < 0 && errno == EINTR);
    if (waited == 0) {
        return false;
    }
    if (waited < 0) {
        result.exit_code = 128;
        result.diagnostics = std::string("waitpid failed: ") + std::strerror(errno);
        return true;
    }
    result.exit_code = status_to_exit_code(status);
    return true;
}

} // namespace btrfsbackup::platform::linux
