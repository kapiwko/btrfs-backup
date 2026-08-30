// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/ProcessSpawn.hpp>

#include <spawn.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <map>
#include <string>
#include <vector>

#include <core/Errors.hpp>

namespace btrfsbackup::platform::linux {

namespace {

class SpawnFileActions final {
  public:
    SpawnFileActions() noexcept
        : error_(posix_spawn_file_actions_init(&value_)) {
    }

    ~SpawnFileActions() noexcept {
        if (initialized()) {
            posix_spawn_file_actions_destroy(&value_);
        }
    }

    SpawnFileActions(const SpawnFileActions&) = delete;
    SpawnFileActions& operator=(const SpawnFileActions&) = delete;

    [[nodiscard]] bool initialized() const noexcept {
        return error_ == 0;
    }

    [[nodiscard]] int error() const noexcept {
        return error_;
    }

    [[nodiscard]] posix_spawn_file_actions_t& get() noexcept {
        return value_;
    }

  private:
    posix_spawn_file_actions_t value_{};
    int error_;
};

class SpawnAttributes final {
  public:
    SpawnAttributes() noexcept
        : error_(posix_spawnattr_init(&value_)) {
    }

    ~SpawnAttributes() noexcept {
        if (initialized()) {
            posix_spawnattr_destroy(&value_);
        }
    }

    SpawnAttributes(const SpawnAttributes&) = delete;
    SpawnAttributes& operator=(const SpawnAttributes&) = delete;

    [[nodiscard]] bool initialized() const noexcept {
        return error_ == 0;
    }

    [[nodiscard]] int error() const noexcept {
        return error_;
    }

    [[nodiscard]] posix_spawnattr_t& get() noexcept {
        return value_;
    }

  private:
    posix_spawnattr_t value_{};
    int error_;
};

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

void validate_environment_entry(const std::string& name, const std::string& value) {
    if (name.empty() || name.find('=') != std::string::npos || name.find('\0') != std::string::npos) {
        throw ValidationError("invalid environment variable name");
    }
    if (value.find('\0') != std::string::npos) {
        throw ValidationError("invalid environment variable value for " + name);
    }
}

std::vector<std::string> child_environment(const ProcessSpawnOptions& options) {
    std::map<std::string, std::string> environment{
        {"HOME", "/root"},
        {"LANG", "C.UTF-8"},
        {"LC_ALL", "C.UTF-8"},
        {"PATH", "/usr/bin"},
    };
    for (const auto& [name, value] : options.environment) {
        validate_environment_entry(name, value);
        environment.insert_or_assign(name, value);
    }

    std::vector<std::string> result;
    result.reserve(environment.size());
    for (const auto& [name, value] : environment) {
        result.push_back(name + "=" + value);
    }
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

    const std::string executable = trusted_program_path(argv.front());
    std::vector<char*> arguments = argv_for_spawn(argv);
    std::vector<std::string> environment = child_environment(options);
    std::vector<char*> environment_entries = argv_for_spawn(environment);
    SpawnFileActions actions;
    if (!actions.initialized()) {
        return {.error = actions.error()};
    }

    int error = add_dup2(actions.get(), options.stdin_fd, STDIN_FILENO);
    if (error == 0) {
        error = add_dup2(actions.get(), options.stdout_fd, STDOUT_FILENO);
    }
    if (error == 0) {
        error = add_dup2(actions.get(), options.stderr_fd, STDERR_FILENO);
    }
    for (int inherited_fd : options.inherited_fds) {
        if (error == 0 && inherited_fd >= 0) {
            error = posix_spawn_file_actions_adddup2(&actions.get(), inherited_fd, inherited_fd);
        }
    }
    if (error != 0) {
        return {.error = error};
    }

    SpawnAttributes attributes;
    if (!attributes.initialized()) {
        return {.error = attributes.error()};
    }

    sigset_t child_mask;
    sigemptyset(&child_mask);
    error = posix_spawnattr_setsigmask(&attributes.get(), &child_mask);

    sigset_t child_defaults;
    sigemptyset(&child_defaults);
    sigaddset(&child_defaults, SIGINT);
    sigaddset(&child_defaults, SIGTERM);
    sigaddset(&child_defaults, SIGPIPE);
    if (error == 0) {
        error = posix_spawnattr_setsigdefault(&attributes.get(), &child_defaults);
    }

    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    if (options.create_process_group) {
        if (error == 0) {
            error = posix_spawnattr_setpgroup(&attributes.get(), 0);
        }
        flags |= POSIX_SPAWN_SETPGROUP;
    }
    if (error == 0) {
        error = posix_spawnattr_setflags(&attributes.get(), flags);
    }
    if (error != 0) {
        return {.error = error};
    }

    pid_t pid = -1;
    error = posix_spawn(
        &pid,
        executable.c_str(),
        &actions.get(),
        &attributes.get(),
        arguments.data(),
        environment_entries.data()
    );
    if (error != 0) {
        return {.error = error};
    }
    return {.pid = pid};
}

} // namespace btrfsbackup::platform::linux
