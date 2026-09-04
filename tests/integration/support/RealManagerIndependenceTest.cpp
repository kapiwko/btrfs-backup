// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealManagerIndependenceTest.hpp"

#include "IntegrationTestProcess.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {
constexpr auto timeout = std::chrono::seconds(120);

[[nodiscard]] std::string read_file(const fs::path& path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open " + path.string());
    const std::string value{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad())
        throw std::runtime_error("cannot read " + path.string());
    return value;
}

void require_success(std::vector<std::string> arguments, std::string_view operation) {
    const auto result = run_test_process(std::move(arguments), timeout);
    if (result.status != 0)
        throw std::runtime_error(std::string(operation) + " failed: " + command_diagnostic(result));
}

[[nodiscard]] fs::path latest(const fs::path& directory) {
    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(directory))
        if (entry.is_directory() && entry.path().filename().string().starts_with("home-"))
            paths.push_back(entry.path());
    if (paths.empty())
        throw std::runtime_error("snapshot not found in " + directory.string());
    return *std::ranges::max_element(paths);
}

[[nodiscard]] std::string subvolume_field(const std::string& output, std::string_view field) {
    std::size_t position = 0;
    while (position < output.size()) {
        const std::size_t end = output.find('\n', position);
        const std::string_view line(output.data() + position, end - position);
        const std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string_view::npos && line.substr(first).starts_with(field))
            return trim_output(std::string(line.substr(first + field.size())));
        if (end == std::string::npos)
            break;
        position = end + 1U;
    }
    return {};
}

[[nodiscard]] std::string lowercase(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}
} // namespace

RealManagerIndependenceTest::RealManagerIndependenceTest(
    fs::path runtime,
    fs::path profile,
    fs::path test_root,
    fs::path source_mount,
    fs::path target_mount
)
    : runtime_(fs::canonical(runtime)),
      profile_(fs::canonical(profile)),
      source_mount_(fs::canonical(source_mount)),
      target_mount_(fs::canonical(target_mount)),
      marker_(fs::canonical(test_root) / "manager-independence.marker"),
      fifo_(fs::canonical(test_root) / "manager-independence.release"),
      log_(fs::canonical(test_root) / "logs/manager-independence.log"),
      original_profile_(read_file(profile_)) {
    const char* consent = std::getenv("BTRFSBACKUP_REAL_BTRFS_CONTAINER");
    if (geteuid() != 0 || consent == nullptr || std::string_view(consent) != "1")
        throw std::runtime_error("manager-independence test requires root in its disposable container");
}

RealManagerIndependenceTest::~RealManagerIndependenceTest() noexcept {
    if (!closed_)
        static_cast<void>(release_resources());
}

void RealManagerIndependenceTest::start_runner() {
    const int output = open(log_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (output < 0)
        throw std::runtime_error("cannot open manager-independence runner log");
    runner_pid_ = fork();
    if (runner_pid_ < 0) {
        ::close(output);
        throw std::runtime_error("cannot fork manager-independence runner");
    }
    if (runner_pid_ == 0) {
        if (setpgid(0, 0) != 0 || dup2(output, STDOUT_FILENO) < 0 ||
            dup2(output, STDERR_FILENO) < 0)
            _exit(127);
        ::close(output);
        if (setenv("INVOCATION_ID", "manager-independence-test", 1) != 0)
            _exit(127);
        execl(runtime_.c_str(), runtime_.c_str(), "--force", "--no-eject", nullptr);
        _exit(127);
    }
    ::close(output);
    if (setpgid(runner_pid_, runner_pid_) != 0 && errno != EACCES && errno != ESRCH)
        throw std::runtime_error("cannot create runner process group");
}

void RealManagerIndependenceTest::wait_for_hook() const {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (fs::exists(marker_) && read_file(marker_) == "started\n")
            return;
        if (kill(runner_pid_, 0) != 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    throw std::runtime_error("runner did not enter the blocking hook: " + read_file(log_));
}

void RealManagerIndependenceTest::release_hook() {
    const int descriptor = open(fifo_.c_str(), O_WRONLY | O_CLOEXEC);
    if (descriptor < 0 || write(descriptor, "continue\n", 9) != 9) {
        if (descriptor >= 0)
            ::close(descriptor);
        throw std::runtime_error("cannot release manager-independence hook");
    }
    ::close(descriptor);
}

void RealManagerIndependenceTest::wait_for_runner() {
    int status = 0;
    while (waitpid(runner_pid_, &status, 0) < 0 && errno == EINTR) {}
    runner_pid_ = -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("runner failed after manager stop: " + read_file(log_));
}

void RealManagerIndependenceTest::verify_snapshots() const {
    const fs::path local = latest(source_mount_ / ".snapshots/home");
    const fs::path remote = latest(target_mount_ / "snapshots/home");
    require_success({"diff", "-qr", local.string(), remote.string()}, "compare manager-independent backup");
    const auto local_info = run_test_process({"btrfs", "subvolume", "show", local.string()}, timeout);
    const auto remote_info = run_test_process({"btrfs", "subvolume", "show", remote.string()}, timeout);
    if (local_info.status != 0 || remote_info.status != 0)
        throw std::runtime_error("cannot inspect manager-independent snapshot UUID lineage");
    const std::string uuid = lowercase(subvolume_field(local_info.output, "UUID:"));
    const std::string received_uuid = lowercase(subvolume_field(remote_info.output, "Received UUID:"));
    if (uuid.empty() || uuid != received_uuid)
        throw std::runtime_error("manager-independent snapshot UUID lineage does not match");
    const std::string history = read_file("/var/lib/btrfs-backup/history/default/last.json");
    if (Json::parse(history).value("state", "") != "succeeded")
        throw std::runtime_error("runner did not persist successful history after manager stop");
}

void RealManagerIndependenceTest::run() {
    Json profile = Json::parse(original_profile_);
    profile["hooks"]["beforeSnapshot"] = Json::array({{{"type", "program"},
        {"program", hook_.string()}, {"arguments", Json::array()}, {"timeoutSeconds", 30}}});
    write_test_file(profile_, profile.dump(2) + "\n");
    profile_modified_ = true;
    if (chmod(profile_.c_str(), 0600) != 0 || mkfifo(fifo_.c_str(), 0600) != 0)
        throw std::runtime_error("cannot prepare manager-independence profile or FIFO");
    write_test_file(hook_, "#!/bin/sh\nprintf 'started\\n' > '" + marker_.string() +
        "'\nIFS= read -r release < '" + fifo_.string() + "'\nprintf 'finished\\n' > '" +
        marker_.string() + "'\n");
    if (chmod(hook_.c_str(), 0755) != 0 || chown(hook_.c_str(), 0, 0) != 0)
        throw std::runtime_error("cannot prepare manager-independence hook");
    require_success({"systemctl", "reload", "dbus.service"}, "reload system D-Bus");
    manager_started_ = true;
    require_success({"systemctl", "start", "btrfs-backupd.service"}, "start system manager");
    require_success({"systemctl", "is-active", "--quiet", "btrfs-backupd.service"}, "verify system manager");
    start_runner();
    wait_for_hook();
    require_success({"systemctl", "stop", "btrfs-backupd.service"}, "stop system manager");
    manager_started_ = false;
    if (run_test_process({"systemctl", "is-active", "--quiet", "btrfs-backupd.service"}, timeout).status == 0)
        throw std::runtime_error("system manager remained active after stop");
    if (kill(runner_pid_, 0) != 0)
        throw std::runtime_error("stopping the manager terminated the active runner");
    release_hook();
    wait_for_runner();
    if (read_file(marker_) != "finished\n")
        throw std::runtime_error("runner did not complete the blocking hook");
    verify_snapshots();
}

std::vector<std::string> RealManagerIndependenceTest::release_resources() noexcept {
    std::vector<std::string> errors;
    if (runner_pid_ > 0) {
        kill(-runner_pid_, SIGKILL);
        while (waitpid(runner_pid_, nullptr, 0) < 0 && errno == EINTR) {}
        runner_pid_ = -1;
    }
    if (manager_started_)
        static_cast<void>(run_test_process({"systemctl", "stop", "btrfs-backupd.service"}, timeout));
    if (profile_modified_) {
        try {
            write_test_file(profile_, original_profile_);
            if (chmod(profile_.c_str(), 0600) != 0)
                errors.emplace_back("restore profile permissions");
            else
                profile_modified_ = false;
        } catch (const std::exception& error) {
            errors.push_back(error.what());
        }
    }
    for (const auto& path : {hook_, marker_, fifo_}) {
        std::error_code error;
        fs::remove(path, error);
        if (error)
            errors.push_back("remove " + path.string() + ": " + error.message());
    }
    return errors;
}

void RealManagerIndependenceTest::close() {
    const auto errors = release_resources();
    if (!errors.empty())
        throw std::runtime_error("manager-independence cleanup failed: " + join_test_errors(errors));
    closed_ = true;
}

} // namespace btrfsbackup::integration
