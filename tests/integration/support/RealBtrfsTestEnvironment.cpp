// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealBtrfsTestEnvironment.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>
#include <strings.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace btrfsbackup::integration {
namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

[[nodiscard]] std::vector<char*> argument_pointers(std::vector<std::string>& arguments) {
    std::vector<char*> pointers;
    pointers.reserve(arguments.size() + 1U);
    for (auto& argument : arguments)
        pointers.push_back(argument.data());
    pointers.push_back(nullptr);
    return pointers;
}

[[nodiscard]] CommandResult run_process(
    std::vector<std::string> arguments,
    std::chrono::seconds timeout,
    std::string_view standard_input = {}
) {
    constexpr std::size_t maximum_output_bytes = 1024U * 1024U;
    const auto create_memory_file = [](const char* name) {
        const int descriptor = memfd_create(name, MFD_CLOEXEC);
        if (descriptor < 0)
            throw std::runtime_error(std::string("memfd_create failed for ") + name);
        return descriptor;
    };
    const auto close_all = [](const std::array<int, 3>& descriptors) {
        for (const int descriptor : descriptors)
            if (descriptor >= 0)
                close(descriptor);
    };
    std::array<int, 3> descriptors{-1, -1, -1};
    try {
        descriptors[0] = create_memory_file("btrfs-backup-test-input");
        descriptors[1] = create_memory_file("btrfs-backup-test-stdout");
        descriptors[2] = create_memory_file("btrfs-backup-test-stderr");
    } catch (...) {
        close_all(descriptors);
        throw;
    }
    const auto write_all = [](int descriptor, std::string_view content) {
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
    };
    try {
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
        const rlimit output_limit{maximum_output_bytes, maximum_output_bytes};
        if (setrlimit(RLIMIT_FSIZE, &output_limit) != 0)
            _exit(127);
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
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            exited = true;
            break;
        }
        if (result < 0 && errno != EINTR)
            break;
        usleep(10'000);
    }
    if (!exited) {
        kill(-child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        close_all(descriptors);
        throw std::runtime_error("command timed out: " + arguments.front());
    }
    const auto read_output = [&](int descriptor, std::string_view stream_name) {
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
                    "command " + std::string(stream_name) + " exceeded 1 MiB: " + arguments.front()
                );
            output.append(buffer.data(), static_cast<std::size_t>(count));
        }
        return output;
    };
    try {
        auto output = read_output(descriptors[1], "stdout");
        auto error_output = read_output(descriptors[2], "stderr");
        close_all(descriptors);
        const int exit_status = WIFEXITED(status) ? WEXITSTATUS(status)
                                                  : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 127);
        return {exit_status, std::move(output), std::move(error_output)};
    } catch (...) {
        close_all(descriptors);
        throw;
    }
}

[[nodiscard]] std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    const auto last = value.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string diagnostic(const CommandResult& result) {
    if (result.error_output.empty())
        return result.output;
    if (result.output.empty())
        return result.error_output;
    return result.output + "\n" + result.error_output;
}

void wipe_secret(std::string& secret) noexcept {
    explicit_bzero(secret.data(), secret.size());
}

void write_file(const fs::path& path, std::string_view content) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    output << content;
    output.close();
    if (!output)
        throw std::runtime_error("cannot write " + path.string());
}

[[nodiscard]] std::string join_errors(const std::vector<std::string>& errors) {
    std::string result;
    for (const auto& error : errors) {
        if (!result.empty())
            result += "; ";
        result += error;
    }
    return result;
}

} // namespace

RealBtrfsTestEnvironment::RealBtrfsTestEnvironment(fs::path backupctl)
    : backupctl_(fs::canonical(backupctl)) {
    const std::string base = (fs::temp_directory_path() / "btrfs-backup-raii.XXXXXX").string();
    std::vector<char> pattern(base.begin(), base.end());
    pattern.push_back('\0');
    const char* created = mkdtemp(pattern.data());
    if (created == nullptr)
        throw std::runtime_error("mkdtemp failed");
    root_ = created;
    source_image_ = root_ / "source.img";
    target_image_ = root_ / "target.img";
    source_mount_ = root_ / "source";
    target_mount_root_ = root_ / "targets";
    target_mount_ = target_mount_root_ / "raii";
    config_root_ = root_ / "config";
    state_root_ = root_ / "state";
    status_root_ = root_ / "status";
    history_root_ = root_ / "history";
    mapper_name_ = "bb-real-raii-" + root_.filename().string();
    mapper_path_ = fs::path("/dev/mapper") / mapper_name_;
    write_file(root_ / ".btrfs-backup-test-root", "managed real-Btrfs fixture\n");
}

RealBtrfsTestEnvironment::~RealBtrfsTestEnvironment() noexcept {
    if (!closed_)
        static_cast<void>(release_resources());
}

CommandResult RealBtrfsTestEnvironment::command(
    std::vector<std::string> arguments,
    std::chrono::seconds timeout,
    std::string_view standard_input
) const {
    return run_process(std::move(arguments), timeout, standard_input);
}

void RealBtrfsTestEnvironment::require_command(
    std::vector<std::string> arguments,
    std::string_view operation
) const {
    const auto result = command(std::move(arguments));
    if (result.status != 0)
        throw std::runtime_error(std::string(operation) + " failed: " + diagnostic(result));
}

void RealBtrfsTestEnvironment::prepare() {
    if (geteuid() != 0)
        throw std::runtime_error("real Btrfs backup test must run as root");
    require_command({"truncate", "-s", "512M", source_image_.string()}, "create source image");
    require_command({"truncate", "-s", "512M", target_image_.string()}, "create target image");

    auto attached = command({"losetup", "--find", "--show", source_image_.string()});
    if (attached.status != 0)
        throw std::runtime_error("attach source loop failed: " + diagnostic(attached));
    source_loop_ = trim(std::move(attached.output));
    verify_owned_loop(source_loop_, source_image_);
    attached = command({"losetup", "--find", "--show", target_image_.string()});
    if (attached.status != 0)
        throw std::runtime_error("attach target loop failed: " + diagnostic(attached));
    target_loop_ = trim(std::move(attached.output));
    verify_owned_loop(target_loop_, target_image_);

    require_command({"mkfs.btrfs", "-q", "-f", source_loop_}, "format source filesystem");
    std::string passphrase = "btrfs-backup-raii-test-passphrase\n";
    auto encrypted = command(
        {"cryptsetup",
         "luksFormat",
         "--batch-mode",
         "--type",
         "luks2",
         "--pbkdf",
         "pbkdf2",
         "--pbkdf-force-iterations",
         "1000",
         "--key-file",
         "-",
         target_loop_},
        std::chrono::seconds(30),
        passphrase
    );
    if (encrypted.status != 0) {
        wipe_secret(passphrase);
        throw std::runtime_error("format encrypted target failed: " + diagnostic(encrypted));
    }
    encrypted = command(
        {"cryptsetup", "open", "--key-file", "-", target_loop_, mapper_name_},
        std::chrono::seconds(30),
        passphrase
    );
    wipe_secret(passphrase);
    if (encrypted.status != 0)
        throw std::runtime_error("open encrypted target failed: " + diagnostic(encrypted));
    mapper_open_ = true;
    require_command({"udevadm", "settle", "--timeout=10"}, "settle target mapper");
    require_command({"dmsetup", "mknodes", mapper_name_}, "materialize target mapper node");
    if (!fs::is_block_file(mapper_path_))
        throw std::runtime_error("target mapper node is missing: " + mapper_path_.string());
    const auto mapper_dependencies =
        command({"dmsetup", "deps", "--noheadings", "-o", "devname", mapper_name_});
    const std::string expected_dependency = "(" + fs::path(target_loop_).filename().string() + ")";
    if (mapper_dependencies.status != 0 || !mapper_dependencies.output.contains(expected_dependency))
        throw std::runtime_error("target mapper does not use the owned loop: " + diagnostic(mapper_dependencies));
    require_command({"mkfs.btrfs", "-q", "-f", mapper_path_.string()}, "format target filesystem");
    fs::create_directories(source_mount_);
    fs::create_directories(target_mount_);
    require_command({"mount", "-o", "noatime", source_loop_, source_mount_.string()}, "mount source filesystem");
    source_mounted_ = true;
    require_command({"mount", "-o", "noatime,nodev,nosuid,noexec,nosymfollow", mapper_path_.string(), target_mount_.string()}, "mount target filesystem");
    target_mounted_ = true;

    require_command({"btrfs", "subvolume", "create", (source_mount_ / "home").string()}, "create source subvolume");
    fs::create_directories(source_mount_ / ".snapshots/home");
    fs::create_directories(target_mount_ / "snapshots");
    fs::create_directories(target_mount_ / ".incoming");
    fs::create_directories(state_root_);
    fs::create_directories(status_root_);
    fs::create_directories(history_root_);

    const auto uuid = command({"blkid", "-s", "UUID", "-o", "value", mapper_path_.string()});
    if (uuid.status != 0 || trim(uuid.output).empty())
        throw std::runtime_error("read target filesystem UUID failed: " + diagnostic(uuid));
    write_configuration(trim(uuid.output));
    write_source_file("file-a.txt", "alpha\n");
    write_source_file("dir/file-b.txt", "first generation\n");
}

void RealBtrfsTestEnvironment::write_configuration(const std::string& target_uuid) const {
    write_file(
        config_root_ / "btrfs-backup.conf",
        "CONFIG_VERSION=1\nSTATE_ROOT=" + state_root_.string() + "\nSTATUS_ROOT=" + status_root_.string() +
            "\nHISTORY_ROOT=" + history_root_.string() + "\nTARGET_MOUNT_ROOT=" + target_mount_root_.string() + "\n"
    );
    const auto luks_uuid_result = command({"cryptsetup", "luksUUID", target_loop_});
    if (luks_uuid_result.status != 0 || trim(luks_uuid_result.output).empty())
        throw std::runtime_error("read target LUKS UUID failed: " + diagnostic(luks_uuid_result));
    const Json profile = {
        {"schemaVersion", 4},
        {"configurationGeneration", "0123456789abcdef0123456789abcdef"},
        {"profileId", "raii"},
        {"name", "RAII real Btrfs"},
        {"enabled", true},
        {"target",
         {{"device", target_loop_},
          {"luksUuid", trim(luks_uuid_result.output)},
          {"btrfsUuid", target_uuid},
          {"partitionUuid", ""},
          {"serial", ""},
          {"mapperName", mapper_name_},
          {"activation", {{"mode", "askPassword"}}}}},
        {"paths",
         {{"remoteRoot", (target_mount_ / "snapshots").string()},
          {"incomingRoot", (target_mount_ / ".incoming").string()}}},
        {"settings",
         {{"dailyLimit", false},
          {"incrementalRequired", true},
          {"keepFailedLocalSnapshot", false},
          {"autoEject", false},
          {"remoteRetention", 2},
          {"localRetention", 2},
          {"minimumTargetFreeBytes", 0},
          {"minimumLocalFreeBytes", 0}}},
        {"hooks", {{"beforeSnapshot", Json::array()}, {"afterSnapshot", Json::array()}}},
        {"sources",
         Json::array({{{"id", "home"}, {"name", "home"}, {"enabled", true}, {"subvolume", (source_mount_ / "home").string()}, {"localSnapshotDir", (source_mount_ / ".snapshots/home").string()}, {"remoteSubdir", "home"}, {"remoteRetention", 2}, {"localRetention", 2}}})}
    };
    write_file(config_root_ / "profiles/raii/profile.json", profile.dump() + "\n");
}

CommandResult RealBtrfsTestEnvironment::execute_backup(
    std::string_view timestamp,
    std::string_view run_id
) const {
    return command(
        {backupctl_.string(),
         "--profile-dir",
         config_root_.string(),
         "--status-root",
         status_root_.string(),
         "--history-root",
         history_root_.string(),
         "runner",
         "execute",
         "--profile",
         "raii",
         "--timestamp",
         std::string(timestamp),
         "--today",
         std::string(timestamp.substr(0, 10)),
         "--run-id",
         std::string(run_id),
         "--force"},
        std::chrono::seconds(60)
    );
}

void RealBtrfsTestEnvironment::write_source_file(
    const fs::path& relative_path,
    std::string_view content
) const {
    if (relative_path.empty() || relative_path.is_absolute() || relative_path.string().starts_with(".."))
        throw std::runtime_error("source file path must be relative");
    write_file(source_mount_ / "home" / relative_path, content);
}

void RealBtrfsTestEnvironment::create_interrupted_receive_artifact() const {
    const fs::path artifact = target_mount_ / ".incoming/home/interrupted-run";
    fs::create_directories(artifact);
    const fs::path partial_subvolume = artifact / "home-interrupted";
    require_command(
        {"btrfs", "subvolume", "create", partial_subvolume.string()},
        "create interrupted receive subvolume"
    );
    write_file(partial_subvolume / "partial-stream", "uncommitted\n");
}

std::size_t RealBtrfsTestEnvironment::local_snapshot_count() const {
    return static_cast<std::size_t>(
        std::ranges::count_if(fs::directory_iterator(source_mount_ / ".snapshots/home"), [](const auto& entry) {
            return entry.is_directory() && entry.path().filename().string().starts_with("home-");
        })
    );
}

std::size_t RealBtrfsTestEnvironment::remote_snapshot_count() const {
    const fs::path root = target_mount_ / "snapshots/home";
    if (!fs::exists(root))
        return 0;
    return static_cast<std::size_t>(
        std::ranges::count_if(fs::directory_iterator(root), [](const auto& entry) {
            return entry.is_directory() && entry.path().filename().string().starts_with("home-");
        })
    );
}

bool RealBtrfsTestEnvironment::incoming_is_empty() const {
    const fs::path root = target_mount_ / ".incoming/home";
    return !fs::exists(root) || fs::is_empty(root);
}

std::string RealBtrfsTestEnvironment::artifact_report() const {
    const auto device_identity = [](const std::string& device) -> Json {
        struct stat status{};
        if (device.empty() || stat(device.c_str(), &status) != 0 || !S_ISBLK(status.st_mode))
            return nullptr;
        return {{"major", major(status.st_rdev)}, {"minor", minor(status.st_rdev)}};
    };
    return Json({
                    {"root", root_.string()},
                    {"marker", (root_ / ".btrfs-backup-test-root").string()},
                    {"sourceImage", source_image_.string()},
                    {"sourceLoop", source_loop_},
                    {"sourceDevice", device_identity(source_loop_)},
                    {"sourceMount", source_mount_.string()},
                    {"targetImage", target_image_.string()},
                    {"targetLoop", target_loop_},
                    {"targetDevice", device_identity(target_loop_)},
                    {"targetMapper", mapper_path_.string()},
                    {"targetMount", target_mount_.string()},
                    {"configRoot", config_root_.string()},
                    {"stateRoot", state_root_.string()},
                    {"statusRoot", status_root_.string()},
                    {"historyRoot", history_root_.string()},
                    {"sourceMounted", source_mounted_},
                    {"targetMounted", target_mounted_},
                    {"mapperOpen", mapper_open_},
                })
        .dump();
}

void RealBtrfsTestEnvironment::verify_owned_loop(
    const std::string& device,
    const fs::path& image
) const {
    struct stat status{};
    if (stat(device.c_str(), &status) != 0 || !S_ISBLK(status.st_mode) || major(status.st_rdev) != 7U)
        throw std::runtime_error("losetup returned an invalid loop device: " + device);
    const auto backing = command({"losetup", "--noheadings", "--output", "BACK-FILE", device});
    if (backing.status != 0 || trim(backing.output) != image.string())
        throw std::runtime_error("loop device is not owned by the fixture: " + device);
}

fs::path RealBtrfsTestEnvironment::latest_snapshot(const fs::path& root) const {
    std::vector<fs::path> snapshots;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.is_directory() && entry.path().filename().string().starts_with("home-"))
            snapshots.push_back(entry.path());
    }
    if (snapshots.empty())
        throw std::runtime_error("snapshot directory is empty: " + root.string());
    return *std::ranges::max_element(snapshots);
}

void RealBtrfsTestEnvironment::require_latest_snapshots_match() const {
    const fs::path local = latest_snapshot(source_mount_ / ".snapshots/home");
    const fs::path remote = latest_snapshot(target_mount_ / "snapshots/home");
    require_command({"diff", "-qr", local.string(), remote.string()}, "compare latest snapshots");
    const auto local_metadata = command({"btrfs", "subvolume", "show", local.string()});
    const auto remote_metadata = command({"btrfs", "subvolume", "show", remote.string()});
    if (local_metadata.status != 0 || remote_metadata.status != 0)
        throw std::runtime_error("cannot read latest snapshot metadata");
    const std::string uuid_marker = "UUID:";
    const std::string received_marker = "Received UUID:";
    const auto local_position = local_metadata.output.find(uuid_marker);
    const auto remote_position = remote_metadata.output.find(received_marker);
    if (local_position == std::string::npos || remote_position == std::string::npos)
        throw std::runtime_error("latest snapshot metadata omitted UUID");
    const auto local_end = local_metadata.output.find('\n', local_position);
    const auto remote_end = remote_metadata.output.find('\n', remote_position);
    const std::string local_uuid = trim(local_metadata.output.substr(local_position + uuid_marker.size(), local_end - local_position - uuid_marker.size()));
    const std::string received_uuid = trim(remote_metadata.output.substr(remote_position + received_marker.size(), remote_end - remote_position - received_marker.size()));
    if (local_uuid.empty() || local_uuid != received_uuid)
        throw std::runtime_error("remote Received UUID does not match the latest local snapshot");
}

std::vector<std::string> RealBtrfsTestEnvironment::release_resources() noexcept {
    std::vector<std::string> errors;
    const auto cleanup = [&](const std::vector<std::string>& arguments, std::string_view operation) {
        try {
            const auto result = run_process(arguments, std::chrono::seconds(10));
            if (result.status != 0)
                errors.push_back(std::string(operation) + ": " + diagnostic(result));
            return result.status == 0;
        } catch (const std::exception& error) {
            errors.push_back(std::string(operation) + ": " + error.what());
            return false;
        }
    };

    if (target_mounted_ && cleanup({"umount", target_mount_.string()}, "unmount target"))
        target_mounted_ = false;
    if (source_mounted_ && cleanup({"umount", source_mount_.string()}, "unmount source"))
        source_mounted_ = false;
    if (mapper_open_ && !target_mounted_) {
        bool mapper_is_owned = false;
        try {
            verify_owned_loop(target_loop_, target_image_);
            const auto dependencies = run_process(
                {"dmsetup", "deps", "--noheadings", "-o", "devname", mapper_name_},
                std::chrono::seconds(10)
            );
            const std::string expected = "(" + fs::path(target_loop_).filename().string() + ")";
            if (dependencies.status != 0 || !dependencies.output.contains(expected)) {
                errors.push_back("target mapper ownership changed; refusing close: " + diagnostic(dependencies));
            } else {
                mapper_is_owned = true;
            }
        } catch (const std::exception& error) {
            errors.push_back(std::string("inspect target mapper ownership: ") + error.what());
        }
        if (mapper_is_owned && cleanup({"cryptsetup", "close", mapper_name_}, "close target mapper"))
            mapper_open_ = false;
    }
    const auto detach_owned_loop = [&](std::string& device, const fs::path& image, std::string_view label) {
        if (device.empty())
            return;
        try {
            const auto backing = run_process(
                {"losetup", "--noheadings", "--output", "BACK-FILE", device},
                std::chrono::seconds(10)
            );
            if (backing.status != 0) {
                errors.push_back(
                    std::string("inspect ") + std::string(label) + " loop ownership: " +
                    diagnostic(backing)
                );
                return;
            }
            if (trim(backing.output) != image.string()) {
                errors.push_back(std::string(label) + " loop ownership changed; refusing detach: " + device);
                return;
            }
            if (cleanup({"losetup", "-d", device}, std::string("detach ") + std::string(label) + " loop"))
                device.clear();
        } catch (const std::exception& error) {
            errors.push_back(std::string("inspect ") + std::string(label) + " loop: " + error.what());
        }
    };
    if (!target_mounted_ && !mapper_open_)
        detach_owned_loop(target_loop_, target_image_, "target");
    if (!source_mounted_)
        detach_owned_loop(source_loop_, source_image_, "source");
    if (!target_mounted_ && !source_mounted_ && !mapper_open_ && target_loop_.empty() && source_loop_.empty()) {
        if (!fs::is_regular_file(root_ / ".btrfs-backup-test-root")) {
            errors.push_back("test-root marker is missing; refusing removal: " + root_.string());
        } else {
            std::error_code error;
            fs::remove_all(root_, error);
            if (error)
                errors.push_back("remove test root: " + error.message());
        }
    }
    closed_ = errors.empty();
    return errors;
}

void RealBtrfsTestEnvironment::close() {
    const auto errors = release_resources();
    if (!errors.empty())
        throw std::runtime_error("real Btrfs cleanup failed: " + join_errors(errors));
}

} // namespace btrfsbackup::integration
