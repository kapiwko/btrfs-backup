#include <btrfsbackup/process_spawn.hpp>

#include <spawn.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <vector>

#include <btrfsbackup/errors.hpp>

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

int add_dup2(posix_spawn_file_actions_t& actions, int source_fd, int target_fd) {
    if (source_fd < 0) {
        return 0;
    }
    return posix_spawn_file_actions_adddup2(&actions, source_fd, target_fd);
}

} // namespace

ProcessSpawnResult spawn_program(const std::vector<std::string>& argv, const ProcessSpawnOptions& options) {
    if (argv.empty()) {
        throw ValidationError("empty command");
    }

    std::vector<char*> arguments = argv_for_spawn(argv);
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
    error = posix_spawnp(
        &pid,
        arguments.front(),
        &actions,
        &attributes,
        arguments.data(),
        environ
    );

    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) {
        return {.error = error};
    }
    return {.pid = pid};
}

} // namespace btrfsbackup
