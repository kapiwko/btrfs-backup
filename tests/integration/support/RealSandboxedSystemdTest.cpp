// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealSandboxedSystemdTest.hpp"

#include "IntegrationTestProcess.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
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

constexpr auto command_timeout = std::chrono::seconds(120);
constexpr std::string_view backup_unit = "btrfs-backup@default.service";
constexpr std::string_view eject_unit = "btrfs-backup-eject@default.service";

[[nodiscard]] CommandResult command(std::vector<std::string> arguments) {
    return run_test_process(std::move(arguments), command_timeout);
}

[[nodiscard]] std::string require_output(
    std::vector<std::string> arguments,
    std::string_view operation
) {
    const auto result = command(std::move(arguments));
    const std::string output = trim_output(result.output);
    if (result.status != 0 || output.empty())
        throw std::runtime_error(std::string(operation) + " failed: " + command_diagnostic(result));
    return output;
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

void append_file(const fs::path& path, std::string_view content) {
    std::ofstream output(path, std::ios::app);
    output << content;
    output.close();
    if (!output)
        throw std::runtime_error("cannot append " + path.string());
}

[[nodiscard]] fs::path latest_snapshot(const fs::path& directory) {
    std::vector<fs::path> snapshots;
    for (const auto& entry : fs::directory_iterator(directory))
        if (entry.is_directory() && entry.path().filename().string().starts_with("home-"))
            snapshots.push_back(entry.path());
    if (snapshots.empty())
        throw std::runtime_error("latest snapshot not found in " + directory.string());
    return *std::ranges::max_element(snapshots);
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

RealSandboxedSystemdTest::RealSandboxedSystemdTest(
    fs::path source_mount,
    fs::path target_mount,
    fs::path staging_mount,
    std::string mapper_name,
    fs::path profile
)
    : source_mount_(fs::canonical(source_mount)),
      target_mount_(fs::canonical(target_mount)),
      staging_mount_(std::move(staging_mount)),
      mapper_name_(std::move(mapper_name)),
      mapper_path_(fs::path("/dev/mapper") / mapper_name_),
      profile_(fs::canonical(profile)) {
    const char* consent = std::getenv("BTRFSBACKUP_REAL_BTRFS_CONTAINER");
    if (geteuid() != 0 || consent == nullptr || std::string_view(consent) != "1")
        throw std::runtime_error("real sandboxed systemd test requires root in its disposable container");
    if (mapper_name_.empty() || mapper_name_.contains('/') || staging_mount_.is_relative())
        throw std::runtime_error("invalid sandboxed systemd fixture arguments");
    mount_unit_path_ = fs::path("/etc/systemd/system") / mount_unit();
    original_profile_ = read_file(profile_);
    original_mount_unit_ = read_file(mount_unit_path_);
    original_mount_dependency_ = read_file(mount_dependency_path_);
}

RealSandboxedSystemdTest::~RealSandboxedSystemdTest() noexcept {
    if (!closed_)
        static_cast<void>(release_resources());
}

void RealSandboxedSystemdTest::require_success(
    std::vector<std::string> arguments,
    std::string_view operation
) const {
    const auto result = command(std::move(arguments));
    if (result.status != 0)
        throw std::runtime_error(std::string(operation) + " failed: " + command_diagnostic(result));
}

std::string RealSandboxedSystemdTest::mount_unit() const {
    return require_output(
        {"systemd-escape", "-p", "--suffix=mount", target_mount_.string()},
        "derive sandbox mount unit"
    );
}

std::string RealSandboxedSystemdTest::property(
    std::string_view unit,
    std::string_view name
) const {
    return require_output(
        {"systemctl", "show", "-P", std::string(name), std::string(unit)},
        "read systemd property " + std::string(name)
    );
}

std::size_t RealSandboxedSystemdTest::eject_completion_count() const {
    const auto journal = command({"journalctl", "--no-pager", "-u", std::string(eject_unit), "-o", "cat"});
    if (journal.status != 0)
        throw std::runtime_error("read eject journal failed: " + command_diagnostic(journal));
    constexpr std::string_view marker = "Finished Safely eject Btrfs backup target for profile default.";
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = journal.output.find(marker, position)) != std::string::npos) {
        ++count;
        position += marker.size();
    }
    return count;
}

void RealSandboxedSystemdTest::wait_for_eject_service(std::size_t previous_count) const {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (eject_completion_count() > previous_count && property(eject_unit, "ActiveState") == "inactive")
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    throw std::runtime_error("timed out waiting for the target eject service");
}

void RealSandboxedSystemdTest::require_latest_snapshots_match() const {
    const fs::path local = latest_snapshot(source_mount_ / ".snapshots/home");
    const fs::path remote = latest_snapshot(target_mount_ / "snapshots/home");
    require_success({"diff", "-qr", local.string(), remote.string()}, "compare latest snapshots");
    const std::string local_details = require_output(
        {"btrfs", "subvolume", "show", local.string()},
        "inspect latest local snapshot"
    );
    const std::string remote_details = require_output(
        {"btrfs", "subvolume", "show", remote.string()},
        "inspect latest remote snapshot"
    );
    const std::string uuid = lowercase(subvolume_field(local_details, "UUID:"));
    const std::string received_uuid = lowercase(subvolume_field(remote_details, "Received UUID:"));
    if (uuid.empty() || uuid != received_uuid)
        throw std::runtime_error("remote Received UUID does not match latest local snapshot UUID");
}

void RealSandboxedSystemdTest::run_sandboxed_backup() {
    append_file(source_mount_ / "home/file-a.txt", "systemd sandbox\n");
    require_success({"sync", "-f", source_mount_.string()}, "sync sandbox source");
    fs::create_directories(staging_mount_);
    require_success(
        {"mount", "--move", target_mount_.string(), staging_mount_.string()},
        "move target to sandbox staging"
    );
    if (command({"findmnt", "-n", "-M", target_mount_.string()}).status == 0)
        throw std::runtime_error("target remained mounted before sandboxed service test");
    system_files_modified_ = true;
    write_test_file(
        mount_unit_path_,
        "[Unit]\nDescription=Disposable bind mount for sandbox test\n\n[Mount]\nWhat=" +
            staging_mount_.string() + "\nWhere=" + target_mount_.string() + "\nType=none\nOptions=bind\n"
    );
    require_success({"systemctl", "daemon-reload"}, "reload sandbox mount unit");
    if (property(backup_unit, "NoNewPrivileges") != "yes" ||
        property(backup_unit, "ProtectSystem") != "full" ||
        property(backup_unit, "MemoryDenyWriteExecute") != "yes")
        throw std::runtime_error("systemd did not apply the required backup sandbox properties");
    const std::size_t previous_ejects = eject_completion_count();
    require_success({"systemctl", "start", std::string(backup_unit)}, "run sandboxed backup service");
    wait_for_eject_service(previous_ejects);
    if (property(backup_unit, "Result") != "success")
        throw std::runtime_error("sandboxed systemd service did not finish successfully");
    require_success({"findmnt", "-n", "-M", target_mount_.string()}, "verify sandbox target mount");
    if (Json::parse(read_file("/var/lib/btrfs-backup/history/default/last.json")).at("state") != "succeeded")
        throw std::runtime_error("sandboxed service did not publish successful history");
    require_latest_snapshots_match();
    if (!fs::is_empty(target_mount_ / ".incoming/home"))
        throw std::runtime_error("incoming source directory is not empty after sandboxed backup");
}

void RealSandboxedSystemdTest::run_automatic_eject() {
    const std::string unit = mount_unit();
    require_success({"systemctl", "stop", unit}, "stop disposable sandbox mount");
    fs::remove(mount_unit_path_);
    write_test_file(mount_dependency_path_, "[Unit]\n");
    require_success({"systemctl", "daemon-reload"}, "reload automatic-eject units");
    require_success(
        {"mount", "--move", staging_mount_.string(), target_mount_.string()},
        "restore target mount before automatic eject"
    );
    Json profile = Json::parse(read_file(profile_));
    profile["settings"]["autoEject"] = true;
    write_test_file(profile_, profile.dump(2) + "\n");
    if (chmod(profile_.c_str(), 0600) != 0)
        throw std::runtime_error("cannot protect automatic-eject profile");
    append_file(source_mount_ / "home/file-a.txt", "automatic eject\n");
    require_success({"sync", "-f", source_mount_.string()}, "sync automatic-eject source");
    const std::size_t previous_ejects = eject_completion_count();
    require_success({"systemctl", "start", std::string(backup_unit)}, "run automatic-eject backup service");
    wait_for_eject_service(previous_ejects);
    if (property(eject_unit, "Result") != "success")
        throw std::runtime_error("target eject service did not finish successfully");
    if (command({"findmnt", "-n", "-M", target_mount_.string()}).status == 0)
        throw std::runtime_error("target remained mounted after the eject service");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (command({"cryptsetup", "status", mapper_name_}).status != 0 && !fs::exists(mapper_path_))
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    throw std::runtime_error("LUKS mapper remained open after the eject service");
}

std::vector<std::string> RealSandboxedSystemdTest::release_resources() noexcept {
    std::vector<std::string> errors;
    if (!system_files_modified_)
        return errors;
    try {
        static_cast<void>(command({"systemctl", "stop", mount_unit()}));
        write_test_file(profile_, original_profile_);
        if (chmod(profile_.c_str(), 0600) != 0)
            errors.emplace_back("restore profile permissions");
        write_test_file(mount_unit_path_, original_mount_unit_);
        write_test_file(mount_dependency_path_, original_mount_dependency_);
        const auto reload = command({"systemctl", "daemon-reload"});
        if (reload.status != 0)
            errors.push_back("reload restored units: " + command_diagnostic(reload));
    } catch (const std::exception& error) {
        errors.push_back(error.what());
    }
    if (errors.empty())
        system_files_modified_ = false;
    return errors;
}

void RealSandboxedSystemdTest::close() {
    const auto errors = release_resources();
    if (!errors.empty())
        throw std::runtime_error("sandboxed systemd cleanup failed: " + join_test_errors(errors));
    closed_ = true;
}

} // namespace btrfsbackup::integration
