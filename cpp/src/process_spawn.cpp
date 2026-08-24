#include <btrfsbackup/process_spawn.hpp>

#include <spawn.h>
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
    posix_spawnattr_t* attributes_ptr = nullptr;
    bool attributes_initialized = false;
    if (options.create_process_group) {
        error = posix_spawnattr_init(&attributes);
        attributes_initialized = error == 0;
        if (error == 0) {
            error = posix_spawnattr_setpgroup(&attributes, 0);
        }
        if (error == 0) {
            error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
        }
        if (error != 0) {
            if (attributes_initialized) {
                posix_spawnattr_destroy(&attributes);
            }
            posix_spawn_file_actions_destroy(&actions);
            return {.error = error};
        }
        attributes_ptr = &attributes;
    }

    pid_t pid = -1;
    error = posix_spawnp(
        &pid,
        arguments.front(),
        &actions,
        attributes_ptr,
        arguments.data(),
        environ
    );

    if (attributes_ptr != nullptr) {
        posix_spawnattr_destroy(&attributes);
    }
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) {
        return {.error = error};
    }
    return {.pid = pid};
}

} // namespace btrfsbackup
