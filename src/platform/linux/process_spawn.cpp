#include <platform/linux/process_spawn.hpp>

#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <config/errors.hpp>

extern char** environ;

namespace btrfsbackup {

namespace {

std::vector<char*> argv_for_spawn(const std::vector<std::string>& argv) {
    std::vector<char*> result;
    result.reserve(argv.size() + 1);
    for (const std::string& item : argv) {
        result.push_back(const_cast<char*>(item.c_str()));
    }
    result.push_back(nullptr);
    return result;
}

std::string trusted_program_path(const std::string& program) {
    if (program.empty()) {
        throw ValidationError("command program must not be empty");
    }
    if (program.front() == '/') {
        return program;
    }
    if (program.find('/') != std::string::npos) {
        throw ValidationError("command path must be absolute: " + program);
    }
    return "/usr/bin/" + program;
}

std::vector<std::string> child_environment() {
    std::vector<std::string> result;
    for (char** entry = environ; *entry != nullptr; ++entry) {
        std::string value = *entry;
        if (value.rfind("PATH=", 0) != 0) {
            result.push_back(std::move(value));
        }
    }
    result.emplace_back("PATH=/usr/bin");
    return result;
}

int add_dup2(posix_spawn_file_actions_t& actions, int source_fd, int target_fd) {
    if (source_fd < 0) {
        return 0;
    }
    return posix_spawn_file_actions_adddup2(&actions, source_fd, target_fd);
}

} // namespace

ChildProcess::ChildProcess(pid_t pid, bool process_group, ChildProcessCleanupPolicy cleanup_policy) noexcept
    : pid_(pid),
      process_group_(process_group),
      owned_(pid > 0),
      cleanup_policy_(cleanup_policy) {
}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept
    : pid_(other.pid_),
      process_group_(other.process_group_),
      owned_(other.owned_),
      leader_reaped_(other.leader_reaped_),
      cleanup_policy_(other.cleanup_policy_) {
    other.release();
}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this != &other) {
        cleanup();
        pid_ = other.pid_;
        process_group_ = other.process_group_;
        owned_ = other.owned_;
        leader_reaped_ = other.leader_reaped_;
        cleanup_policy_ = other.cleanup_policy_;
        other.release();
    }
    return *this;
}

ChildProcess::~ChildProcess() {
    cleanup();
}

pid_t ChildProcess::pid() const {
    return pid_;
}

bool ChildProcess::process_group_exists() const {
    if (pid_ <= 0) {
        return false;
    }
    pid_t target = process_group_ ? -pid_ : pid_;
    if (kill(target, 0) == 0) {
        return true;
    }
    return errno != ESRCH;
}

void ChildProcess::send_signal(int signal) const {
    if (pid_ <= 0) {
        return;
    }
    pid_t target = process_group_ ? -pid_ : pid_;
    if (kill(target, signal) != 0 && process_group_ && errno == ESRCH) {
        kill(pid_, signal);
    }
}

void ChildProcess::mark_reaped() {
    leader_reaped_ = true;
    owned_ = false;
}

void ChildProcess::release() {
    owned_ = false;
}

bool ChildProcess::wait_until(std::chrono::steady_clock::time_point deadline) noexcept {
    while (true) {
        if (!leader_reaped_) {
            int status = 0;
            pid_t waited;
            do {
                waited = waitpid(pid_, &status, WNOHANG);
            } while (waited < 0 && errno == EINTR);
            if (waited == pid_ || (waited < 0 && errno == ECHILD)) {
                leader_reaped_ = true;
            }
        }
        if (leader_reaped_ && (!process_group_ || !process_group_exists())) {
            owned_ = false;
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void ChildProcess::cleanup() noexcept {
    if (!owned_ || pid_ <= 0) {
        return;
    }

    send_signal(SIGTERM);
    if (wait_until(std::chrono::steady_clock::now() + cleanup_policy_.terminate_grace_period)) {
        return;
    }
    send_signal(SIGKILL);
    wait_until(std::chrono::steady_clock::now() + cleanup_policy_.kill_reap_period);

    if (!leader_reaped_) {
        int status = 0;
        pid_t ignored;
        do {
            ignored = waitpid(pid_, &status, WNOHANG);
        } while (ignored < 0 && errno == EINTR);
    }
    owned_ = false;
}

ProcessSpawnResult spawn_program(const std::vector<std::string>& argv, const ProcessSpawnOptions& options) {
    if (argv.empty()) {
        throw ValidationError("empty command");
    }

    const std::string executable = trusted_program_path(argv.front());
    std::vector<char*> arguments = argv_for_spawn(argv);
    std::vector<std::string> environment = child_environment();
    std::vector<char*> environment_entries = argv_for_spawn(environment);
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error != 0) {
        return {.error = error};
    }

    error = add_dup2(actions, options.stdin_fd, STDIN_FILENO);
    if (error == 0) {
        error = add_dup2(actions, options.stdout_fd, STDOUT_FILENO);
    }
    if (error == 0) {
        error = add_dup2(actions, options.stderr_fd, STDERR_FILENO);
    }
    for (int inherited_fd : options.inherited_fds) {
        if (error == 0 && inherited_fd >= 0) {
            error = posix_spawn_file_actions_adddup2(&actions, inherited_fd, inherited_fd);
        }
    }
    if (error != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return {.error = error};
    }

    posix_spawnattr_t attributes;
    error = posix_spawnattr_init(&attributes);
    if (error != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return {.error = error};
    }

    sigset_t child_mask;
    sigemptyset(&child_mask);
    error = posix_spawnattr_setsigmask(&attributes, &child_mask);

    sigset_t child_defaults;
    sigemptyset(&child_defaults);
    sigaddset(&child_defaults, SIGINT);
    sigaddset(&child_defaults, SIGTERM);
    sigaddset(&child_defaults, SIGPIPE);
    if (error == 0) {
        error = posix_spawnattr_setsigdefault(&attributes, &child_defaults);
    }

    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    if (options.create_process_group) {
        if (error == 0) {
            error = posix_spawnattr_setpgroup(&attributes, 0);
        }
        flags |= POSIX_SPAWN_SETPGROUP;
    }
    if (error == 0) {
        error = posix_spawnattr_setflags(&attributes, flags);
    }
    if (error != 0) {
        posix_spawnattr_destroy(&attributes);
        posix_spawn_file_actions_destroy(&actions);
        return {.error = error};
    }

    pid_t pid = -1;
    error = posix_spawn(
        &pid,
        executable.c_str(),
        &actions,
        &attributes,
        arguments.data(),
        environment_entries.data()
    );

    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) {
        return {.error = error};
    }
    return {.pid = pid};
}

} // namespace btrfsbackup
