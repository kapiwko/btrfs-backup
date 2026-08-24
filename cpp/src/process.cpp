#include <btrfsbackup/process.hpp>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/process_spawn.hpp>

namespace btrfsbackup {

CommandResult run_command(const std::vector<std::string>& argv) {
    if (argv.empty()) {
        throw ValidationError("empty command");
    }
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) != 0) {
        throw ValidationError("cannot create pipe");
    }
    ProcessSpawnResult spawned = spawn_program(argv, {
        .stdout_fd = pipefd[1],
        .stderr_fd = pipefd[1],
    });
    close(pipefd[1]);
    CommandResult result;
    if (!spawned.started()) {
        close(pipefd[0]);
        result.exit_code = 127;
        result.output = "cannot spawn " + argv.front() + ": " + std::strerror(spawned.error);
        return result;
    }
    ChildProcess child(spawned.pid, false);
    char buffer[4096];
    while (true) {
        ssize_t count = read(pipefd[0], buffer, sizeof(buffer));
        if (count > 0) {
            result.output.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        result.output += std::string("cannot read command output: ") + std::strerror(errno);
        break;
    }
    close(pipefd[0]);
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(spawned.pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        throw ValidationError(std::string("cannot wait for command: ") + std::strerror(errno));
    }
    child.mark_reaped();
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = 128;
    }
    return result;
}

std::string run_capture(const std::vector<std::string>& argv) {
    CommandResult result = run_command(argv);
    if (result.exit_code != 0) {
        throw ValidationError("command failed: " + argv.front());
    }
    while (!result.output.empty() && (result.output.back() == '\n' || result.output.back() == '\r')) {
        result.output.pop_back();
    }
    return result.output;
}

} // namespace btrfsbackup
