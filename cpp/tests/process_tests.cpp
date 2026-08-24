#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <stdexcept>
#include <string>

#include <btrfsbackup/process.hpp>
#include <btrfsbackup/process_spawn.hpp>

#include "test_helpers.hpp"

namespace {

void test_run_command_captures_stdout_and_stderr() {
    btrfsbackup::CommandResult result = btrfsbackup::run_command({
        "sh",
        "-c",
        "printf output; printf error >&2; exit 7",
    });

    test_helpers::expect_eq("command exit", std::to_string(result.exit_code), "7");
    test_helpers::expect_contains("command stdout", result.output, "output");
    test_helpers::expect_contains("command stderr", result.output, "error");
}

void test_run_command_reports_missing_executable() {
    btrfsbackup::CommandResult result = btrfsbackup::run_command({
        "/definitely-missing-btrfsbackup-command",
    });

    test_helpers::expect_eq("missing command exit", std::to_string(result.exit_code), "127");
    test_helpers::expect_contains("missing command diagnostics", result.output, "cannot spawn");
    test_helpers::expect_contains(
        "missing command name",
        result.output,
        "/definitely-missing-btrfsbackup-command"
    );
}

void test_child_process_reaps_group_during_exception_unwind() {
    int ready_pipe[2];
    test_helpers::expect_eq(
        "create child readiness pipe",
        std::to_string(pipe2(ready_pipe, O_CLOEXEC)),
        "0"
    );
    btrfsbackup::ProcessSpawnResult spawned = btrfsbackup::spawn_program(
        {"sh", "-c", "trap '' TERM; printf r; while :; do sleep 1; done"},
        {
            .stdout_fd = ready_pipe[1],
            .create_process_group = true,
        }
    );
    close(ready_pipe[1]);
    test_helpers::expect_true("spawn RAII child", spawned.started(), "child did not start");
    const pid_t child_pid = spawned.pid;
    auto started_at = std::chrono::steady_clock::now();

    try {
        btrfsbackup::ChildProcess child(
            child_pid,
            true,
            {
                .terminate_grace_period = std::chrono::milliseconds(50),
                .kill_reap_period = std::chrono::milliseconds(500),
            }
        );
        char ready = 0;
        test_helpers::expect_eq("RAII child ready", std::to_string(read(ready_pipe[0], &ready, 1)), "1");
        throw std::runtime_error("injected failure after spawn");
    } catch (const std::runtime_error&) {
    }
    close(ready_pipe[0]);

    int status = 0;
    errno = 0;
    pid_t waited = waitpid(child_pid, &status, WNOHANG);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at
    ).count();
    test_helpers::expect_eq("RAII child already reaped", std::to_string(waited), "-1");
    test_helpers::expect_eq("RAII child wait status", std::to_string(errno), std::to_string(ECHILD));
    test_helpers::expect_true("RAII cleanup bounded", elapsed_ms < 1500, "child cleanup exceeded its deadline");
}

} // namespace

int main() {
    test_run_command_captures_stdout_and_stderr();
    test_run_command_reports_missing_executable();
    test_child_process_reaps_group_during_exception_unwind();

    return test_helpers::finish("process tests");
}
