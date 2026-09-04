// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "IntegrationTestProcess.hpp"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace btrfsbackup::integration {
namespace {

[[nodiscard]] std::vector<char*> argument_pointers(std::vector<std::string>& arguments) {
    std::vector<char*> pointers;
    pointers.reserve(arguments.size() + 1U);
    for (auto& argument : arguments)
        pointers.push_back(argument.data());
    pointers.push_back(nullptr);
    return pointers;
}

[[nodiscard]] int create_memory_file(const char* name) {
    const int descriptor = memfd_create(name, MFD_CLOEXEC);
    if (descriptor < 0)
        throw std::runtime_error(std::string("memfd_create failed for ") + name);
    return descriptor;
}

void close_all(const std::array<int, 3>& descriptors) noexcept {
    for (const int descriptor : descriptors)
        if (descriptor >= 0)
            close(descriptor);
}

void write_all(int descriptor, std::string_view content) {
    std::size_t written = 0;
    while (written < content.size()) {
        const ssize_t count = write(descriptor, content.data() + written, content.size() - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            throw std::runtime_error("cannot prepare command standard input");
        written += static_cast<std::size_t>(count);
    }
    if (lseek(descriptor, 0, SEEK_SET) < 0)
        throw std::runtime_error("cannot rewind command standard input");
}

[[nodiscard]] std::string read_output(
    int descriptor,
    std::string_view stream_name,
    std::string_view program
) {
    constexpr std::size_t maximum_output_bytes = 1024U * 1024U;
    if (lseek(descriptor, 0, SEEK_SET) < 0)
        throw std::runtime_error("cannot rewind command " + std::string(stream_name));
    std::string output;
    std::array<char, 4096> buffer{};
    for (;;) {
        const ssize_t count = read(descriptor, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        if (output.size() + static_cast<std::size_t>(count) > maximum_output_bytes)
            throw std::runtime_error(
                "command " + std::string(stream_name) + " exceeded 1 MiB: " + std::string(program)
            );
        output.append(buffer.data(), static_cast<std::size_t>(count));
    }
    return output;
}

} // namespace

CommandResult run_test_process(
    std::vector<std::string> arguments,
    std::chrono::seconds timeout,
    std::string_view standard_input
) {
    constexpr std::size_t maximum_output_bytes = 1024U * 1024U;
    std::array<int, 3> descriptors{-1, -1, -1};
    try {
        descriptors[0] = create_memory_file("btrfs-backup-test-input");
        descriptors[1] = create_memory_file("btrfs-backup-test-stdout");
        descriptors[2] = create_memory_file("btrfs-backup-test-stderr");
        write_all(descriptors[0], standard_input);
    } catch (...) {
        close_all(descriptors);
        throw;
    }
    const pid_t child = fork();
    if (child < 0) {
        close_all(descriptors);
        throw std::runtime_error("fork failed");
    }
    if (child == 0) {
        if (setpgid(0, 0) != 0 || dup2(descriptors[0], STDIN_FILENO) < 0 ||
            dup2(descriptors[1], STDOUT_FILENO) < 0 || dup2(descriptors[2], STDERR_FILENO) < 0)
            _exit(127);
        close_all(descriptors);
        if (clearenv() != 0 || setenv("PATH", "/usr/bin:/usr/sbin", 1) != 0 ||
            setenv("LANG", "C.UTF-8", 1) != 0 || setenv("LC_ALL", "C.UTF-8", 1) != 0 ||
            setenv("HOME", "/root", 1) != 0)
            _exit(127);
        auto pointers = argument_pointers(arguments);
        execvp(pointers.front(), pointers.data());
        _exit(127);
    }

    close(descriptors[0]);
    descriptors[0] = -1;
    if (setpgid(child, child) != 0 && errno != EACCES && errno != ESRCH) {
        kill(-child, SIGKILL);
        while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
        close_all(descriptors);
        throw std::runtime_error("cannot create command process group");
    }
    int status = 0;
    bool exited = false;
    bool output_exceeded = false;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            exited = true;
            break;
        }
        if (result < 0 && errno != EINTR)
            break;
        struct stat output_status{};
        struct stat error_status{};
        if ((fstat(descriptors[1], &output_status) == 0 &&
             static_cast<std::uintmax_t>(output_status.st_size) > maximum_output_bytes) ||
            (fstat(descriptors[2], &error_status) == 0 &&
             static_cast<std::uintmax_t>(error_status.st_size) > maximum_output_bytes)) {
            output_exceeded = true;
            break;
        }
        usleep(10'000);
    }
    if (!exited) {
        kill(-child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        close_all(descriptors);
        if (output_exceeded)
            throw std::runtime_error("command output exceeded 1 MiB: " + arguments.front());
        throw std::runtime_error("command timed out: " + arguments.front());
    }
    try {
        auto output = read_output(descriptors[1], "stdout", arguments.front());
        auto error_output = read_output(descriptors[2], "stderr", arguments.front());
        close_all(descriptors);
        const int exit_status = WIFEXITED(status) ? WEXITSTATUS(status)
                                                 : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 127);
        return {exit_status, std::move(output), std::move(error_output)};
    } catch (...) {
        close_all(descriptors);
        throw;
    }
}

std::string trim_output(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    const auto last = value.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : value.substr(first, last - first + 1U);
}

std::string command_diagnostic(const CommandResult& result) {
    if (result.error_output.empty())
        return result.output;
    if (result.output.empty())
        return result.error_output;
    return result.output + "\n" + result.error_output;
}

void wipe_test_secret(std::string& secret) noexcept {
    explicit_bzero(secret.data(), secret.size());
}

void write_test_file(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << content;
    output.close();
    if (!output)
        throw std::runtime_error("cannot write " + path.string());
}

std::string join_test_errors(const std::vector<std::string>& errors) {
    std::string result;
    for (const auto& error : errors) {
        if (!result.empty())
            result += "; ";
        result += error;
    }
    return result;
}

} // namespace btrfsbackup::integration
