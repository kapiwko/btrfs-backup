// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <core/ManagerProtocol.hpp>

#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <regex>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;
namespace fs = std::filesystem;

constexpr std::string_view service = btrfsbackup::manager_protocol::service_name;
constexpr std::string_view object = btrfsbackup::manager_protocol::object_path;
constexpr std::string_view interface = btrfsbackup::manager_protocol::interface_name;

struct CommandResult {
    int status{};
    std::string output;
};

[[nodiscard]] std::vector<char*> argument_pointers(std::vector<std::string>& arguments) {
    std::vector<char*> pointers;
    pointers.reserve(arguments.size() + 1U);
    for (auto& argument : arguments)
        pointers.push_back(argument.data());
    pointers.push_back(nullptr);
    return pointers;
}

[[noreturn]] void child_failure() {
    _exit(127);
}

class ChildProcess final {
  public:
    ChildProcess() = default;

    ChildProcess(
        std::vector<std::string> arguments,
        const fs::path& output_path,
        std::span<const std::pair<std::string, std::string>> environment = {}
    ) {
        const pid_t child = fork();
        if (child < 0)
            throw std::runtime_error("fork failed");
        if (child == 0) {
            const int output = open(output_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
            if (output < 0)
                child_failure();
            if (dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0)
                child_failure();
            close(output);
            for (const auto& [name, value] : environment) {
                if (setenv(name.c_str(), value.c_str(), 1) != 0)
                    child_failure();
            }
            auto pointers = argument_pointers(arguments);
            execv(pointers.front(), pointers.data());
            child_failure();
        }
        pid_ = child;
    }

    ~ChildProcess() noexcept {
        stop(SIGTERM, 500ms);
    }

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    ChildProcess(ChildProcess&& other) noexcept : pid_(std::exchange(other.pid_, -1)) {
    }
    ChildProcess& operator=(ChildProcess&& other) noexcept {
        if (this != &other) {
            stop(SIGTERM, 500ms);
            pid_ = std::exchange(other.pid_, -1);
        }
        return *this;
    }

    [[nodiscard]] bool running() const {
        if (pid_ < 0)
            return false;
        return kill(pid_, 0) == 0;
    }

    [[nodiscard]] int wait(std::chrono::milliseconds timeout) {
        if (pid_ < 0)
            return 0;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        int status = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            const pid_t result = waitpid(pid_, &status, WNOHANG);
            if (result == pid_) {
                pid_ = -1;
                return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            }
            if (result < 0 && errno == EINTR)
                continue;
            if (result < 0 && errno == ECHILD) {
                pid_ = -1;
                return 0;
            }
            if (result < 0)
                throw std::runtime_error("waitpid failed");
            std::this_thread::sleep_for(10ms);
        }
        throw std::runtime_error("child process timeout");
    }

    void stop(int signal, std::chrono::milliseconds timeout) noexcept {
        if (pid_ < 0)
            return;
        kill(pid_, signal);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        int status = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            const pid_t result = waitpid(pid_, &status, WNOHANG);
            if (result == pid_ || (result < 0 && errno == ECHILD)) {
                pid_ = -1;
                return;
            }
            std::this_thread::sleep_for(10ms);
        }
        kill(pid_, SIGKILL);
        while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {}
        pid_ = -1;
    }

  private:
    pid_t pid_{-1};
};

[[nodiscard]] CommandResult run_command(
    std::vector<std::string> arguments,
    std::chrono::milliseconds timeout = 3s,
    std::span<const std::pair<std::string, std::string>> environment = {}
) {
    std::array<int, 2> pipe_descriptors{};
    if (pipe(pipe_descriptors.data()) != 0)
        throw std::runtime_error("pipe failed");
    const pid_t child = fork();
    if (child < 0) {
        close(pipe_descriptors[0]);
        close(pipe_descriptors[1]);
        throw std::runtime_error("fork failed");
    }
    if (child == 0) {
        close(pipe_descriptors[0]);
        if (dup2(pipe_descriptors[1], STDOUT_FILENO) < 0 || dup2(pipe_descriptors[1], STDERR_FILENO) < 0)
            child_failure();
        close(pipe_descriptors[1]);
        for (const auto& [name, value] : environment) {
            if (setenv(name.c_str(), value.c_str(), 1) != 0)
                child_failure();
        }
        auto pointers = argument_pointers(arguments);
        execv(pointers.front(), pointers.data());
        child_failure();
    }

    close(pipe_descriptors[1]);
    const int flags = fcntl(pipe_descriptors[0], F_GETFL, 0);
    fcntl(pipe_descriptors[0], F_SETFL, flags | O_NONBLOCK);
    std::string output;
    int status = 0;
    bool exited = false;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        std::array<char, 4096> buffer{};
        for (;;) {
            const ssize_t count = read(pipe_descriptors[0], buffer.data(), buffer.size());
            if (count > 0)
                output.append(buffer.data(), static_cast<std::size_t>(count));
            else
                break;
        }
        const pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            exited = true;
            break;
        }
        pollfd descriptor{pipe_descriptors[0], POLLIN, 0};
        poll(&descriptor, 1, 10);
    }
    if (!exited) {
        kill(child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        close(pipe_descriptors[0]);
        throw std::runtime_error("command timeout: " + arguments.front());
    }
    std::array<char, 4096> buffer{};
    for (;;) {
        const ssize_t count = read(pipe_descriptors[0], buffer.data(), buffer.size());
        if (count > 0)
            output.append(buffer.data(), static_cast<std::size_t>(count));
        else
            break;
    }
    close(pipe_descriptors[0]);
    return {WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status), std::move(output)};
}

void require(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void require_contains(std::string_view text, std::string_view expected, std::string_view message) {
    require(text.contains(expected), message);
}

[[nodiscard]] std::string read_file(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_file(const fs::path& path, std::string_view contents, mode_t mode = 0600) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    output << contents;
    output.close();
    if (!output || chmod(path.c_str(), mode) != 0)
        throw std::runtime_error("cannot write fixture file: " + path.string());
}

template <typename Predicate>
void wait_until(Predicate predicate, std::chrono::milliseconds timeout, std::string_view failure) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return;
        std::this_thread::sleep_for(50ms);
    }
    throw std::runtime_error(std::string(failure));
}

class Fixture final {
  public:
    Fixture(std::string daemon, std::string dbus_daemon, std::string busctl, std::string policy, std::string polkit, std::vector<std::string> qml)
        : daemon_(std::move(daemon)), dbus_daemon_(std::move(dbus_daemon)), busctl_(std::move(busctl)),
          policy_(std::move(policy)), polkit_(std::move(polkit)), qml_(std::move(qml)) {
        std::array<char, 40> pattern{};
        const std::string base = (fs::temp_directory_path() / "btrfs-backup-dbus.XXXXXX").string();
        std::copy(base.begin(), base.end(), pattern.begin());
        const char* created_root = mkdtemp(pattern.data());
        if (created_root == nullptr)
            throw std::runtime_error("mkdtemp failed");
        root_ = created_root;
        address_ = "unix:path=" + (root_ / "bus").string();
        create_documents();
    }

    ~Fixture() noexcept {
        daemon_process_.stop(SIGTERM, 500ms);
        polkit_process_.stop(SIGTERM, 500ms);
        bus_process_.stop(SIGTERM, 500ms);
        std::error_code error;
        fs::remove_all(root_, error);
    }

    void run() {
        start_bus(false);
        start_polkit();
        start_daemon();
        verify_read_api();
        verify_profile_update_and_signals();
        verify_authorization_and_errors();
        verify_daemon_recovery();
        restart_with_policy();
    }

  private:
    [[nodiscard]] std::vector<std::string> busctl_prefix() const {
        return {busctl_, "--address=" + address_, "--timeout=2"};
    }

    [[nodiscard]] CommandResult call(std::string_view method, std::vector<std::string> arguments = {}) const {
        auto command = busctl_prefix();
        command.insert(command.end(), {"call", std::string(service), std::string(object), std::string(interface), std::string(method)});
        command.insert(command.end(), arguments.begin(), arguments.end());
        return run_command(std::move(command));
    }

    [[nodiscard]] bool name_is_owned(std::string_view name) const {
        auto command = busctl_prefix();
        command.push_back("list");
        const auto result = run_command(std::move(command), 1500ms);
        return result.status == 0 && result.output.contains(name);
    }

    void start_bus(bool restricted) {
        const fs::path config = root_ / (restricted ? "policy-bus.conf" : "test-bus.conf");
        const passwd* user = getpwuid(getuid());
        require(user != nullptr, "cannot resolve test user");
        std::string policies;
        if (restricted) {
            policies = "    <allow send_destination=\"org.freedesktop.DBus\"/>\n"
                       "    <allow receive_sender=\"org.freedesktop.DBus\"/>\n"
                       "    <allow send_requested_reply=\"true\"/>\n"
                       "    <allow receive_requested_reply=\"true\"/>\n";
        } else {
            policies = "    <allow send_destination=\"*\"/>\n    <allow receive_sender=\"*\"/>\n";
        }
        write_file(config, "<busconfig>\n  <type>system</type>\n  <listen>" + address_ + "</listen>\n"
                                                                                         "  <auth>EXTERNAL</auth>\n  <policy context=\"default\">\n    <allow user=\"*\"/>\n" +
                       policies + "  </policy>\n  <policy user=\"" + user->pw_name + "\">\n"
                                                                                     "    <allow own=\"" +
                       std::string(service) + "\"/>\n"
                                              "    <allow own=\"org.freedesktop.PolicyKit1\"/>\n  </policy>\n" +
                       (restricted ? "  <include>" + policy_ + "</include>\n" : "") + "</busconfig>\n");
        bus_process_ = ChildProcess({dbus_daemon_, "--config-file=" + config.string(), "--nofork"}, root_ / "bus.log");
        wait_until([this] { return fs::exists(root_ / "bus"); }, 2500ms, "private bus did not start");
    }

    void start_polkit() {
        polkit_process_ = ChildProcess({polkit_, address_, (root_ / "polkit.log").string(), "500"}, root_ / "polkit-process.log");
        wait_until([this] { return name_is_owned("org.freedesktop.PolicyKit1"); }, 3s, "fake polkit authority did not acquire its bus name");
    }

    void start_daemon() {
        daemon_process_ = ChildProcess({daemon_, "--bus-address", address_, "--config-root", (root_ / "etc").string(), "--public-profile-root", (root_ / "public").string(), "--status-root", (root_ / "status").string(), "--history-root", (root_ / "history").string(), "--target-mount-root", (root_ / "mnt").string(), "--mapper-root", (root_ / "mapper").string(), "--udev-root", (root_ / "udev").string(), "--systemd-root", (root_ / "systemd").string(), "--browse-session-root", (root_ / "browse").string(), "--skip-configuration-activation", "--audit-log", (root_ / "audit/manager.jsonl").string()}, root_ / "daemon.log");
        wait_until([this] {
            if (!daemon_process_.running())
                throw std::runtime_error("daemon exited before acquiring its bus name:\n" + read_file(root_ / "daemon.log"));
            return name_is_owned(service);
        },
                   3s,
                   "daemon did not acquire its bus name");
    }

    void stop_services() noexcept {
        daemon_process_.stop(SIGTERM, 1s);
        polkit_process_.stop(SIGTERM, 1s);
        bus_process_.stop(SIGTERM, 1s);
        std::error_code error;
        fs::remove(root_ / "bus", error);
    }

    void create_documents();
    void verify_read_api();
    void verify_profile_update_and_signals();
    void verify_authorization_and_errors();
    void verify_daemon_recovery();
    void restart_with_policy();

    fs::path root_;
    std::string address_;
    std::string daemon_;
    std::string dbus_daemon_;
    std::string busctl_;
    std::string policy_;
    std::string polkit_;
    std::vector<std::string> qml_;
    ChildProcess bus_process_;
    ChildProcess polkit_process_;
    ChildProcess daemon_process_;
    std::string initial_status_;
    std::string status_before_;
};

void Fixture::create_documents() {
    fs::create_directories(root_ / "etc/profiles/default");
    fs::create_directories(root_ / "public");
    fs::create_directories(root_ / "status/default");
    fs::create_directories(root_ / "history/default");
    fs::create_directories(root_ / "state/profiles/default");
    fs::create_directories(root_ / "mnt");
    fs::create_directories(root_ / "mapper");

    write_file(root_ / "public/default.json", R"({"schemaVersion":1,"profileId":"default","name":"Default backup","target":{"name":"Backup disk"},"sources":[{"id":"home","name":"Home"}]})"
                                              "\n",
               0644);
    write_file(root_ / "etc/btrfs-backup.conf", "CONFIG_VERSION=1\nSTATE_ROOT=" + (root_ / "state").string() + "\n");
    initial_status_ = R"({"schemaVersion":4,"runId":"20260829T160000Z-1-1","operationKind":"backup","state":"running","phase":"sizing","activity":"sizing","canCancel":true,"errorCode":"","sourceName":"Home","targetName":"Backup disk","speedBps":10,"etaSeconds":20,"sourceProgress":30,"overallProgress":40,"progressAccuracy":"estimated","sourceIndex":1,"sourceCount":1,"startedAt":"2026-08-29T15:59:00Z","updatedAt":"2026-08-29T16:00:00Z"})"
                      "\n";
    write_file(root_ / "status/default/current.json", initial_status_, 0644);
    const std::string history = R"({"schemaVersion":2,"profileId":"default","profileName":"Default backup","runId":"20260825T100000Z-1-1","state":"failed","phase":"failed","message":"private","currentSourceName":"Home","targetName":"Backup disk","sourceIndex":1,"sourceCount":1,"startedAt":"2026-08-25T09:59:00Z","updatedAt":"2026-08-25T10:00:00Z","finishedAt":"2026-08-25T10:00:00Z","errorCode":"private.failure","errorMessage":"private failure","details":{"device":"/dev/private"},"recoverable":false,"suggestedAction":"","canCancel":false,"bytesProcessed":40,"bytesTotalEstimated":100,"runBytesProcessed":40,"speedBps":0,"etaSeconds":-1,"sourceProgress":40,"overallProgress":40,"progressAccuracy":"exact","exitCode":1})"
                                "\n";
    write_file(root_ / "history/default/20260825T100000Z-1-1.json", history);
    write_file(root_ / "history/default/last.json", history);
    write_file(root_ / "state/profiles/default/last-success", "date=2026-08-24\ntimestamp=2026-08-24T18:42:00+0000\n");
    write_file(root_ / "etc/profiles/default/profile.json", R"({"schemaVersion":4,"configurationGeneration":"0123456789abcdef0123456789abcdef","profileId":"default","name":"Default backup","enabled":true,"target":{"device":"/dev/null","luksUuid":"11111111-2222-3333-4444-555555555555","btrfsUuid":"66666666-7777-8888-9999-aaaaaaaaaaaa","mapperName":"backupdisk","activation":{"mode":"askPassword"}},"sources":[{"id":"home","name":"Home","enabled":true,"subvolume":"/home","localSnapshotDir":"/.snapshots/home","remoteSubdir":"home","remoteRetention":2,"localRetention":2}]})"
                                                            "\n");
}

void Fixture::verify_read_api() {
    const auto capabilities = call("GetCapabilities");
    require(capabilities.status == 0, "GetCapabilities failed");
    require_contains(capabilities.output, "readOnly", "capabilities omit readOnly");
    require_contains(capabilities.output, "start-backup", "capabilities omit operational control");
    require_contains(capabilities.output, "change-signals", "capabilities omit change signals");

    const auto profiles = call("ListProfiles");
    require(profiles.status == 0, "ListProfiles failed");
    require_contains(profiles.output, "Default backup", "public profile was not returned");
    const auto status = call("GetStatus", {"s", "default"});
    require(status.status == 0, "GetStatus failed");
    require_contains(status.output, "state", "current status omits state");
    require_contains(status.output, "running", "current state was not returned");
    status_before_ = status.output;

    if (qml_.size() == 3U) {
        const std::array environment{
            std::pair<std::string, std::string>{"DBUS_SYSTEM_BUS_ADDRESS", address_},
            std::pair<std::string, std::string>{"QT_QPA_PLATFORM", "offscreen"},
            std::pair<std::string, std::string>{"QT_FORCE_STDERR_LOGGING", "1"},
        };
        ChildProcess qml({qml_[0], "-I", qml_[1], qml_[2]}, root_ / "qml.log", environment);
        wait_until([this, &qml] {
            require(qml.running(), "Plasma backend exited before loading the initial manager state");
            return read_file(root_ / "qml.log").contains("initial-manager-state-ready");
        },
                   11s,
                   "Plasma backend did not load the initial manager state");
        const fs::path status_path = root_ / "status/default/current.json";
        fs::rename(status_path, status_path.string() + ".running");
        write_file(status_path.string() + ".next", R"({"schemaVersion":4,"runId":"20260829T160000Z-1-1","operationKind":"backup","state":"succeeded","phase":"completed","activity":"idle","canCancel":false,"errorCode":"","sourceName":"Home","targetName":"Backup disk","speedBps":0,"etaSeconds":-1,"sourceProgress":100,"overallProgress":100,"progressAccuracy":"exact","sourceIndex":1,"sourceCount":1,"startedAt":"2026-08-29T15:59:00Z","updatedAt":"2026-08-29T16:00:00Z"})"
                                                   "\n",
                   0644);
        fs::rename(status_path.string() + ".next", status_path);
        require(qml.wait(11s) == 0, "Plasma backend did not consume the manager change signal");
        fs::remove(status_path);
        fs::rename(status_path.string() + ".running", status_path);
    }

    const auto history = call("GetHistorySanitized", {"suu", "default", "0", "1"});
    require(history.status == 0, "GetHistorySanitized failed");
    require_contains(history.output, "backup.failed", "history error was not sanitized");
    require(!history.output.contains("/dev/private"), "private history details crossed the bus");
    const auto device = call("GetDeviceState", {"s", "default"});
    require(device.status == 0, "GetDeviceState failed");
    require_contains(device.output, "connected", "device state was not returned");
    require(!device.output.contains("/dev/null"), "device path crossed the bus");

    auto introspect_command = busctl_prefix();
    introspect_command.insert(introspect_command.end(), {"introspect", std::string(service), std::string(object), std::string(interface)});
    const auto introspection = run_command(std::move(introspect_command));
    require(introspection.status == 0, "manager introspection failed");
    constexpr std::array methods{
        "GetCapabilities",
        "ListProfiles",
        "GetStatus",
        "GetHistorySanitized",
        "GetDeviceState",
        "StartBackup",
        "CancelBackup",
        "ValidateTarget",
        "EjectTarget",
        "GetProfileDetails",
        "UpdateProfileSettings",
        "AddProfileSource",
        "UpdateProfileSource",
        "RemoveProfileSource",
        "DeleteProfile",
        "SetProfileEnabled",
        "OpenBrowseSession",
        "RenewBrowseSession",
        "BeginBrowseOperation",
        "EndBrowseOperation",
        "CloseBrowseSession",
        "ListBrowseDirectory",
        "ListBrowseDirectoryPage",
        "ListPreviousVersions",
        "InspectBrowseEntry",
        "OpenBrowseFile",
        "OpenBrowseEntry",
        "InspectBrowseRepository",
        "ResolveBackupCoverageByFd",
        "ListTargetCredentials",
        "AddTargetPassphrase",
        "AddTargetKey",
        "GenerateTargetKey",
        "RemoveTargetCredential",
        "InspectStorageTopology",
        "InspectExistingTarget",
        "BuildDevicePreparationPlan",
        "ListSourceCandidates",
        "StartDevicePreparation",
        "GetDevicePreparation",
        "CancelDevicePreparation"
    };
    for (const std::string_view method : methods)
        require_contains(introspection.output, method, "manager introspection omits a method");
    require(
        !introspection.output.contains("SetBrowseSessionActive"),
        "manager introspection still exposes the removed counted browse-operation API"
    );
    constexpr std::array signals{"ProfilesChanged", "StatusChanged", "HistoryChanged", "DeviceStateChanged"};
    for (const std::string_view signal : signals)
        require_contains(introspection.output, signal, "manager introspection omits a signal");
}

void Fixture::verify_profile_update_and_signals() {
    const auto details = call("GetProfileDetails", {"s", "default"});
    require(details.status == 0, "GetProfileDetails failed");
    require_contains(details.output, "fingerprint", "profile details omit fingerprint");
    require_contains(details.output, "generation", "profile details omit generation");
    require(!details.output.contains("key contents"), "secret contents crossed profile details");
    const auto begin = std::search_n(details.output.begin(), details.output.end(), 64U, true, [](char value, bool) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
    });
    require(begin != details.output.end(), "profile details returned no fingerprint");
    const std::string fingerprint(begin, begin + 64);

    write_file(root_ / "polkit.log.allow", "");
    const auto saved = call("UpdateProfileSettings", {"ssss", "default", "0123456789abcdef0123456789abcdef", fingerprint, R"({"name":"Edited backup","dailyLimit":false,"autoEject":true})"});
    require(saved.status == 0, "UpdateProfileSettings failed: " + saved.output);
    require_contains(saved.output, "generation", "profile save omitted the new generation");
    const auto profile = read_file(root_ / "etc/profiles/default/profile.json");
    require_contains(profile, "Edited backup", "profile settings update was not published");
    require(
        std::regex_search(
            profile,
            std::regex(R"("configurationGeneration"\s*:\s*"[0-9a-f]{32}")")
        ),
        "profile save did not assign a generation"
    );
    fs::remove(root_ / "polkit.log.allow");

    std::vector<ChildProcess> watchers;
    for (const std::string signal : {"ProfilesChanged", "StatusChanged", "HistoryChanged"}) {
        auto command = busctl_prefix();
        command.insert(command.end(), {"wait", std::string(service), std::string(object), std::string(interface), signal});
        watchers.emplace_back(std::move(command), root_ / (signal + ".log"));
    }
    std::this_thread::sleep_for(100ms);
    for (const fs::path& path : {root_ / "public/default.json", root_ / "status/default/current.json", root_ / "history/default/last.json"}) {
        const fs::path next = path.string() + ".next";
        fs::copy_file(path, next, fs::copy_options::overwrite_existing);
        fs::rename(next, path);
    }
    for (auto& watcher : watchers)
        require(watcher.wait(3s) == 0, "filesystem change did not emit a manager signal");
}

[[nodiscard]] std::vector<std::string> lines(std::string_view text) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t end = text.find('\n', begin);
        result.emplace_back(text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin));
        if (end == std::string_view::npos)
            break;
        begin = end + 1U;
    }
    return result;
}

void Fixture::verify_authorization_and_errors() {
    static_cast<void>(call("StartBackup", {"s", "default"}));
    static_cast<void>(call("ValidateTarget", {"s", "default"}));
    const auto browse = call("OpenBrowseSession", {"s", "default"});
    const int coverage_descriptor = ::open((root_ / "public/default.json").c_str(), O_PATH);
    require(coverage_descriptor >= 0, "cannot open coverage fixture with O_PATH");
    const auto coverage = call("ResolveBackupCoverageByFd", {"h", std::to_string(coverage_descriptor)});
    ::close(coverage_descriptor);
    const int readable_descriptor = ::open((root_ / "public/default.json").c_str(), O_RDONLY);
    require(readable_descriptor >= 0, "cannot open readable coverage fixture");
    const auto invalid_coverage = call("ResolveBackupCoverageByFd", {"h", std::to_string(readable_descriptor)});
    ::close(readable_descriptor);
    require(browse.status != 0, "repository browse was available without authorization");
    require(coverage.status != 0, "backup coverage was available without authorization");
    require(
        invalid_coverage.status != 0 && invalid_coverage.output.contains("request is invalid"),
        "coverage accepted a descriptor that was not opened with O_PATH: " + invalid_coverage.output
    );
    const std::string polkit_log = read_file(root_ / "polkit.log");
    require_contains(polkit_log, "io.github.btrfsbackup.start-backup", "start request used the wrong polkit action");
    require_contains(polkit_log, "io.github.btrfsbackup.validate-target", "validate request used the wrong polkit action");
    require_contains(polkit_log, "io.github.btrfsbackup.manage-profile-configuration", "profile update used the wrong polkit action");
    require_contains(polkit_log, "io.github.btrfsbackup.open-browse-session", "repository access used the wrong polkit action");

    const std::string audit = read_file(root_ / "audit/manager.jsonl");
    require_contains(audit, "\"callerUid\":" + std::to_string(getuid()), "manager audit omitted caller UID");
    require_contains(audit, "\"action\":\"start-backup\"", "manager audit omitted requested action");
    require_contains(audit, "\"profileId\":\"default\"", "manager audit omitted profile");
    require_contains(audit, "\"result\":\"denied\"", "manager audit omitted denied result");
    require_contains(audit, "io.github.btrfsbackup.Error.NotAuthorized", "manager audit omitted denial code");
    struct stat audit_stat{};
    require(stat((root_ / "audit/manager.jsonl").c_str(), &audit_stat) == 0, "cannot stat manager audit");
    require((audit_stat.st_mode & 0777) == 0600, "manager audit is not root-only");

    std::set<std::string> caller_names;
    for (const auto& line : lines(polkit_log)) {
        const auto separator = line.find(' ');
        if (separator != std::string::npos)
            caller_names.insert(line.substr(0, separator));
    }
    require(caller_names.size() >= 2U, "authorization was not bound to each caller connection");

    const std::size_t authorization_lines = lines(polkit_log).size();
    const std::size_t daemon_log_size = read_file(root_ / "daemon.log").size();
    write_file(root_ / "polkit.log.allow", "");
    write_file(root_ / "polkit.log.delay", "");
    auto command = busctl_prefix();
    command.insert(command.end(), {"call", std::string(service), std::string(object), std::string(interface), "StartBackup", "s", "default"});
    ChildProcess disconnected(std::move(command), root_ / "disconnected.log");
    wait_until([this, authorization_lines] {
        return lines(read_file(root_ / "polkit.log")).size() > authorization_lines;
    },
               2s,
               "disconnected caller did not reach polkit");
    disconnected.stop(SIGTERM, 500ms);
    fs::remove(root_ / "polkit.log.allow");
    fs::remove(root_ / "polkit.log.delay");
    wait_until([this, daemon_log_size] {
        const auto log = read_file(root_ / "daemon.log");
        return log.size() > daemon_log_size &&
            log.substr(daemon_log_size).contains("manager operation was not authorized");
    },
               2s,
               "caller disconnect during authorization reached the operational backend");

    const auto invalid = call("GetStatus", {"s", "../invalid"});
    require(invalid.status != 0, "malformed profile id was accepted");
    require_contains(invalid.output, "manager request is invalid", "malformed profile id returned an unsafe error");
    require(call("GetHistorySanitized", {"suu", "default", "0", "101"}).status != 0, "unbounded history limit was accepted");

    const fs::path status = root_ / "status/default/current.json";
    fs::copy_file(status, status.string() + ".valid", fs::copy_options::overwrite_existing);
    write_file(status, "{invalid\n", 0644);
    const auto malformed = call("GetStatus", {"s", "default"});
    require(malformed.status != 0, "malformed status document was accepted");
    require_contains(malformed.output, "manager request is invalid", "malformed status returned an unsafe error");
    require(!malformed.output.contains(root_.string()), "private manager path leaked through a D-Bus error");
    fs::remove(status);
    fs::rename(status.string() + ".valid", status);
    require(call("ListProfiles").status == 0, "daemon stopped after caller disconnect");
    require(daemon_process_.running(), "caller disconnect stopped the daemon");
}

void Fixture::verify_daemon_recovery() {
    daemon_process_.stop(SIGKILL, 1s);
    fs::rename(root_ / "status", root_ / "status.saved");
    start_daemon();
    auto command = busctl_prefix();
    command.insert(command.end(), {"wait", std::string(service), std::string(object), std::string(interface), "StatusChanged"});
    ChildProcess watcher(std::move(command), root_ / "recreated-status-signal.log");
    std::this_thread::sleep_for(100ms);
    fs::create_directories(root_ / "status/default");
    fs::rename(root_ / "status.saved/default/current.json", root_ / "status/default/current.json");
    require(watcher.wait(3s) == 0, "creating an absent status root did not emit StatusChanged");
    const auto status = call("GetStatus", {"s", "default"});
    require(status.status == 0, "GetStatus failed after daemon restart");
    require(status.output == status_before_, "daemon crash recovery did not restore visible state");
}

void Fixture::restart_with_policy() {
    stop_services();
    start_bus(true);
    start_polkit();
    start_daemon();
    require(call("GetCapabilities").status == 0, "policy denied an allowed read method");
    require(call("GetProfileDetails", {"s", "default"}).status == 0, "policy denied unauthenticated profile details");
    const auto start = call("StartBackup", {"s", "default"});
    require(start.status != 0, "test start unexpectedly succeeded");
    require_contains(start.output, "operation is not authorized", "operational call did not pass through bus policy and polkit");
    require(call("InspectStorageTopology").status == 0, "read-only device inventory required authorization");
    require(call("ListSourceCandidates").status == 0, "read-only source discovery required authorization");
    require(call("GetDevicePreparation", {"s", "guessed-operation"}).status != 0, "foreign preparation status was available without authorization");
    require(call("CancelDevicePreparation", {"s", "guessed-operation"}).status != 0, "foreign preparation cancellation was accepted without authorization");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 6 && argc != 9) {
        std::cerr << "usage: DbusManagerTests DAEMON DBUS_DAEMON BUSCTL POLICY POLKIT [QML IMPORT TEST]\n";
        return 2;
    }
    try {
        std::vector<std::string> qml;
        if (argc == 9)
            qml.assign(argv + 6, argv + 9);
        Fixture fixture(argv[1], argv[2], argv[3], argv[4], argv[5], std::move(qml));
        fixture.run();
        std::cout << "ok - private D-Bus manager API\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "dbus-manager-tests: " << error.what() << '\n';
        return 1;
    }
}
