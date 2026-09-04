// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealSystemDbusBackupTest.hpp"

#include "IntegrationTestProcess.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

constexpr auto command_timeout = std::chrono::seconds(120);
constexpr std::string_view test_user = "btrfs-dbus-test";

[[nodiscard]] CommandResult command(std::vector<std::string> arguments) {
    return run_test_process(std::move(arguments), command_timeout);
}

[[nodiscard]] std::string require_output(
    std::vector<std::string> arguments,
    std::string_view operation
) {
    const auto result = command(std::move(arguments));
    if (result.status != 0)
        throw std::runtime_error(std::string(operation) + " failed: " + command_diagnostic(result));
    return trim_output(result.output);
}

void require_success(std::vector<std::string> arguments, std::string_view operation) {
    static_cast<void>(require_output(std::move(arguments), operation));
}

[[nodiscard]] std::string read_file(const fs::path& path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open " + path.string());
    const std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad())
        throw std::runtime_error("cannot read " + path.string());
    return content;
}

[[nodiscard]] std::vector<fs::path> snapshots(const fs::path& directory) {
    std::vector<fs::path> result;
    for (const auto& entry : fs::directory_iterator(directory))
        if (entry.is_directory() && entry.path().filename().string().starts_with("home-"))
            result.push_back(entry.path());
    std::ranges::sort(result);
    return result;
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

RealSystemDbusBackupTest::RealSystemDbusBackupTest(
    fs::path source_mount,
    fs::path target_mount,
    fs::path test_root,
    fs::path client
)
    : source_mount_(fs::canonical(source_mount)),
      target_mount_(fs::canonical(target_mount)),
      test_root_(fs::canonical(test_root)),
      client_(fs::canonical(client)) {
    const char* consent = std::getenv("BTRFSBACKUP_REAL_BTRFS_CONTAINER");
    if (geteuid() != 0 || consent == nullptr || std::string_view(consent) != "1")
        throw std::runtime_error("real system D-Bus test requires root in its disposable container");
}

RealSystemDbusBackupTest::~RealSystemDbusBackupTest() noexcept {
    if (!closed_)
        static_cast<void>(release_resources());
}

void RealSystemDbusBackupTest::require_policy(std::string_view action) const {
    const std::string details = require_output(
        {"pkaction", "--verbose", "--action-id", std::string(action)},
        "inspect installed polkit action " + std::string(action)
    );
    const auto active = details.find("implicit active:");
    if (active == std::string::npos)
        throw std::runtime_error("installed polkit action has no active-session policy: " + std::string(action));
    const auto value_begin = active + std::string_view("implicit active:").size();
    const auto line_end = details.find('\n', active);
    if (trim_output(details.substr(value_begin, line_end - value_begin)) != "yes")
        throw std::runtime_error("installed polkit action requires a password: " + std::string(action));
}

std::string RealSystemDbusBackupTest::call_as_test_user(std::string_view method) const {
    return require_output(
        {"runuser", "-u", std::string(test_user), "--", client_.string(), "--call", std::string(method)},
        "unprivileged D-Bus " + std::string(method)
    );
}

void RealSystemDbusBackupTest::wait_for_backup(std::string_view operation_id) const {
    const fs::path history = "/var/lib/btrfs-backup/history/default/last.json";
    const std::string unit = "btrfs-backup-run@" + std::string(operation_id) + ".service";
    const auto history_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < history_deadline) {
        if (fs::exists(history)) {
            const std::string state = Json::parse(read_file(history)).value("state", "");
            if (state == "succeeded")
                break;
            if (state == "failed" || state == "cancelled")
                throw std::runtime_error("D-Bus backup finished with state " + state);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!fs::exists(history) || Json::parse(read_file(history)).value("state", "") != "succeeded")
        throw std::runtime_error("timed out waiting for D-Bus backup history");

    const auto unit_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < unit_deadline) {
        const auto state = command({"systemctl", "show", "--property=ActiveState", "--value", unit});
        const std::string value = trim_output(state.output);
        if (state.status != 0 || value.empty() || value == "inactive" || value == "failed")
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    throw std::runtime_error("timed out waiting for D-Bus backup unit " + unit);
}

void RealSystemDbusBackupTest::verify_backup() const {
    const fs::path history_path = "/var/lib/btrfs-backup/history/default/last.json";
    if (Json::parse(read_file(history_path)).at("state") != "succeeded")
        throw std::runtime_error("D-Bus backup history is not successful");
    const std::string last_success = read_file("/var/lib/btrfs-backup/profiles/default/last-success");
    if (("\n" + last_success + "\n").find("\nprofile_id=default\n") == std::string::npos)
        throw std::runtime_error("profile last-success state was not written");

    const std::string status = require_output(
        {"btrfs-backupctl", "status", "show", "--profile", "default", "--human"},
        "render backup status"
    );
    if (status.find("Default backup: succeeded") == std::string::npos)
        throw std::runtime_error("btrfs-backupctl did not render successful human status");
    const std::string history = require_output(
        {"btrfs-backupctl", "status", "history", "--profile", "default", "--limit", "1"},
        "render backup history"
    );
    if (history.find("\"state\": \"succeeded\"") == std::string::npos)
        throw std::runtime_error("btrfs-backupctl did not render successful history");

    const auto local = snapshots(source_mount_ / ".snapshots/home");
    const auto remote = snapshots(target_mount_ / "snapshots/home");
    if (local.size() != 1U || remote.size() != 1U)
        throw std::runtime_error("first D-Bus backup did not produce exactly one snapshot pair");
    require_success({"diff", "-qr", local.back().string(), remote.back().string()}, "compare D-Bus snapshots");
    const std::string local_info = require_output(
        {"btrfs", "subvolume", "show", local.back().string()}, "inspect local D-Bus snapshot"
    );
    const std::string remote_info = require_output(
        {"btrfs", "subvolume", "show", remote.back().string()}, "inspect remote D-Bus snapshot"
    );
    const std::string uuid = lowercase(subvolume_field(local_info, "UUID:"));
    const std::string received_uuid = lowercase(subvolume_field(remote_info, "Received UUID:"));
    if (uuid.empty() || uuid != received_uuid)
        throw std::runtime_error("D-Bus backup snapshot UUID lineage does not match");
}

void RealSystemDbusBackupTest::run() {
    require_policy("io.github.btrfsbackup.start-backup");
    require_policy("io.github.btrfsbackup.cancel-backup");
    require_policy("io.github.btrfsbackup.validate-target");
    require_policy("io.github.btrfsbackup.eject-target");
    require_success(
        {"useradd", "--system", "--no-create-home", "--shell", "/usr/bin/nologin", std::string(test_user)},
        "create D-Bus test user"
    );
    user_created_ = true;
    if (require_output({"id", "-u", std::string(test_user)}, "inspect D-Bus test user") == "0")
        throw std::runtime_error("D-Bus integration caller unexpectedly has UID 0");
    write_test_file(
        policy_rule_,
        "polkit.addRule(function(action, subject) {\n"
        "    if (action.id == \"io.github.btrfsbackup.start-backup\" &&\n"
        "        subject.user == \"btrfs-dbus-test\") return polkit.Result.YES;\n"
        "});\n"
    );
    fs::permissions(policy_rule_, fs::perms::owner_read | fs::perms::owner_write |
                                      fs::perms::group_read | fs::perms::others_read);
    fs::remove_all("/run/btrfs-backup/profiles/default");
    fs::remove_all("/var/lib/btrfs-backup/history/default");
    require_success({"systemctl", "reload", "dbus.service"}, "reload system D-Bus");
    services_started_ = true;
    require_success(
        {"systemctl", "start", "polkit.service", "btrfs-backupd.service"},
        "start polkit and manager"
    );
    require_success({"systemctl", "is-active", "--quiet", "polkit.service"}, "verify polkit service");
    require_success(
        {"systemctl", "is-active", "--quiet", "btrfs-backupd.service"}, "verify manager service"
    );

    const Json response = Json::parse(call_as_test_user("StartBackup"));
    if (!response.value("accepted", false) || response.value("operationId", "").empty())
        throw std::runtime_error("manager did not accept the unprivileged start request");
    wait_for_backup(response.at("operationId").get<std::string>());
    require_success({"journalctl", "--sync"}, "synchronize D-Bus backup journal");
    const std::string journal = require_output(
        {"journalctl", "--no-pager", "-o", "cat", "-u",
         "btrfs-backup-run@" + response.at("operationId").get<std::string>() + ".service",
         "-u", "btrfs-backupd.service"},
        "read D-Bus backup journal"
    );
    if (journal.find("\"incremental\": false") == std::string::npos)
        throw std::runtime_error("full stream was not used for first D-Bus backup");
    const Json status = Json::parse(call_as_test_user("GetStatus"));
    if (status.value("state", "") != "succeeded")
        throw std::runtime_error("manager did not reconstruct terminal status from history");
    verify_backup();
}

std::vector<std::string> RealSystemDbusBackupTest::release_resources() noexcept {
    std::vector<std::string> errors;
    if (services_started_) {
        const auto stopped = command({"systemctl", "stop", "btrfs-backupd.service", "polkit.service"});
        if (stopped.status != 0)
            errors.push_back("stop D-Bus test services: " + command_diagnostic(stopped));
        else
            services_started_ = false;
    }
    std::error_code error;
    fs::remove(policy_rule_, error);
    if (error)
        errors.push_back("remove polkit test rule: " + error.message());
    if (user_created_) {
        const auto deleted = command({"userdel", std::string(test_user)});
        if (deleted.status != 0)
            errors.push_back("remove D-Bus test user: " + command_diagnostic(deleted));
        else
            user_created_ = false;
    }
    return errors;
}

void RealSystemDbusBackupTest::close() {
    const auto errors = release_resources();
    if (!errors.empty())
        throw std::runtime_error("system D-Bus cleanup failed: " + join_test_errors(errors));
    closed_ = true;
}

} // namespace btrfsbackup::integration
