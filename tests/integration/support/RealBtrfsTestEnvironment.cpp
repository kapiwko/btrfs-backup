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
#include <sstream>
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

RealBtrfsTestEnvironment::RealBtrfsTestEnvironment(
    fs::path backupctl,
    fs::path browse_session_client
)
    : backupctl_(fs::canonical(backupctl)),
      browse_session_client_(fs::canonical(browse_session_client)) {
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
    const char* container_consent = std::getenv("BTRFSBACKUP_REAL_BTRFS_CONTAINER");
    if (container_consent == nullptr || std::string_view(container_consent) != "1")
        throw std::runtime_error("real Btrfs backup test requires its disposable container marker");
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

void RealBtrfsTestEnvironment::require_target_identity_rejected() const {
    const fs::path profile_path = config_root_ / "profiles/raii/profile.json";
    std::ifstream profile_input(profile_path);
    Json profile = Json::parse(profile_input);
    profile_input.close();
    if (!profile_input)
        throw std::runtime_error("cannot read profile before target identity test");
    const std::string original_profile = profile.dump() + "\n";
    profile["target"]["btrfsUuid"] = "00000000-0000-0000-0000-000000000000";
    write_file(profile_path, profile.dump() + "\n");
    const std::size_t local_before = local_snapshot_count();
    const std::size_t remote_before = remote_snapshot_count();
    CommandResult result;
    try {
        result = execute_backup("2026-08-22T09:00:00Z", "20260822T090000Z-raii-wrong-target");
    } catch (...) {
        write_file(profile_path, original_profile);
        throw;
    }
    write_file(profile_path, original_profile);
    if (result.status == 0 || !diagnostic(result).contains("Btrfs UUID mismatch"))
        throw std::runtime_error("runner did not reject a mismatched target identity: " + diagnostic(result));
    if (local_snapshot_count() != local_before || remote_snapshot_count() != remote_before)
        throw std::runtime_error("target identity rejection changed snapshot state");
}

void RealBtrfsTestEnvironment::write_pending_marker(
    const fs::path& local_snapshot,
    const fs::path& final_snapshot,
    std::string_view run_id
) const {
    const fs::path marker = state_root_ / "profiles/raii/pending-home";
    write_file(
        marker,
        "source_name=home\nlocal_snapshot_path=" + local_snapshot.string() +
            "\nfinal_snapshot_path=" + final_snapshot.string() + "\nrun_id=" + std::string(run_id) +
            "\ntimestamp=2026-08-23T08:00:00Z\n"
    );
    require_command({"chmod", "0600", marker.string()}, "protect pending marker");
    require_command({"sync", "-f", marker.string()}, "sync pending marker");
}

void RealBtrfsTestEnvironment::require_pre_receive_recovery() {
    const fs::path orphan = source_mount_ / ".snapshots/home/home-2026-08-19T080000Z";
    const fs::path final = target_mount_ / "snapshots/home" / orphan.filename();
    require_command(
        {"btrfs", "subvolume", "snapshot", "-r", (source_mount_ / "home").string(), orphan.string()},
        "create pre-receive recovery orphan"
    );
    write_pending_marker(orphan, final, "20260819T080000Z-raii-interrupted");
    const auto result = execute_backup("2026-08-23T08:00:00Z", "20260823T080000Z-raii-recovery-before");
    if (result.status != 0)
        throw std::runtime_error("pre-receive recovery backup failed: " + diagnostic(result));
    if (fs::exists(orphan) || fs::exists(state_root_ / "profiles/raii/pending-home"))
        throw std::runtime_error("pre-receive recovery retained its orphan or pending marker");
    if (!incoming_is_empty())
        throw std::runtime_error("pre-receive recovery retained incoming artifacts");
}

void RealBtrfsTestEnvironment::require_post_commit_recovery() {
    const fs::path local = latest_snapshot(source_mount_ / ".snapshots/home");
    const fs::path remote = latest_snapshot(target_mount_ / "snapshots/home");
    write_pending_marker(local, remote, "20260823T080000Z-raii-committed");
    write_source_file("post-commit-recovery.txt", "recovery generation\n");
    const auto result = execute_backup("2026-08-24T08:00:00Z", "20260824T080000Z-raii-recovery-after");
    if (result.status != 0)
        throw std::runtime_error("post-commit recovery backup failed: " + diagnostic(result));
    if (fs::exists(state_root_ / "profiles/raii/pending-home"))
        throw std::runtime_error("post-commit recovery retained its pending marker");
    if (!fs::is_directory(local) || !fs::is_directory(remote))
        throw std::runtime_error("post-commit recovery removed a committed snapshot pair");
    require_latest_snapshots_match();
    if (!incoming_is_empty())
        throw std::runtime_error("post-commit recovery retained incoming artifacts");
}

std::string RealBtrfsTestEnvironment::subvolume_field(
    const fs::path& subvolume,
    std::string_view field
) const {
    const auto metadata = command({"btrfs", "subvolume", "show", subvolume.string()});
    if (metadata.status != 0)
        throw std::runtime_error("cannot inspect subvolume " + subvolume.string() + ": " + diagnostic(metadata));
    std::istringstream lines(metadata.output);
    std::string line;
    const std::string prefix = std::string(field) + ':';
    while (std::getline(lines, line)) {
        line = trim(std::move(line));
        if (line.starts_with(prefix))
            return trim(line.substr(prefix.size()));
    }
    throw std::runtime_error("subvolume metadata omitted " + std::string(field));
}

void RealBtrfsTestEnvironment::require_restore_scenarios() const {
    const fs::path repository = target_mount_ / "snapshots";
    const fs::path latest_remote = latest_snapshot(repository / "home");

    const fs::path raw_root = source_mount_ / "restore-drill";
    const fs::path raw_stream = root_ / "restore-stream.btrfs";
    const fs::path raw_restored = raw_root / latest_remote.filename();
    fs::create_directories(raw_root);
    require_command({"btrfs", "send", "-f", raw_stream.string(), latest_remote.string()}, "write restore stream");
    require_command({"btrfs", "receive", "-f", raw_stream.string(), raw_root.string()}, "receive restore stream");
    require_command({"diff", "-qr", latest_remote.string(), raw_restored.string()}, "compare raw restore");
    require_command({"btrfs", "subvolume", "delete", raw_restored.string()}, "delete raw restored subvolume");
    const bool removed_raw_stream = fs::remove(raw_stream);
    const bool removed_raw_root = fs::remove(raw_root);
    if (!removed_raw_stream || !removed_raw_root)
        throw std::runtime_error("raw restore cleanup left artifacts");

    const std::string created_at = "2026-08-24T09:00:00Z";
    const std::string relative = "home/" + latest_remote.filename().string();
    const std::string snapshot_uuid = subvolume_field(latest_remote, "UUID");
    const std::string received_uuid = subvolume_field(latest_remote, "Received UUID");
    const auto target_uuid_result = command({"blkid", "-s", "UUID", "-o", "value", mapper_path_.string()});
    const std::string target_uuid = trim(target_uuid_result.output);
    if (target_uuid_result.status != 0 || target_uuid.empty())
        throw std::runtime_error("cannot read restore target UUID: " + diagnostic(target_uuid_result));
    write_file(
        repository / "repository.json",
        Json({{"schemaVersion", 1},
              {"repositoryId", "real-" + target_uuid},
              {"targetFilesystemUuid", target_uuid},
              {"createdAt", created_at},
              {"features", Json::array({"catalog-v1"})}})
                .dump() +
            "\n"
    );
    write_file(
        repository / "catalog.json",
        Json({{"schemaVersion", 1},
              {"generation", 1},
              {"snapshots",
               Json::array({{{"snapshotId", "real-latest"}, {"hostId", "real-host"}, {"profileId", "raii"}, {"sourceId", "home"}, {"relativePath", relative}, {"createdAt", created_at}, {"uuid", snapshot_uuid}, {"receivedUuid", received_uuid}, {"verified", true}}})}})
                .dump() +
            "\n"
    );

    const fs::path restored = source_mount_ / "restore-engine-result";
    const fs::path drill_root = source_mount_ / "restore-engine-drill";
    const fs::path drill_destination = drill_root / "result";
    const auto restore_command = [&](std::string_view operation, const fs::path& destination, std::string_view transaction) {
        std::vector<std::string> arguments{backupctl_.string(), "restore", std::string(operation), "--repository", repository.string(), "--snapshot", "real-latest", "--source", ".", "--destination", destination.string(), "--transaction", std::string(transaction)};
        if (operation != "drill")
            arguments.emplace_back("--subvolume");
        return command(std::move(arguments), std::chrono::seconds(60));
    };
    auto plan = restore_command("plan", restored, "real-plan");
    if (plan.status != 0 || fs::exists(restored))
        throw std::runtime_error("restore plan failed or mutated its destination: " + diagnostic(plan));
    auto restored_result = restore_command("execute", restored, "real-execute");
    if (restored_result.status != 0)
        throw std::runtime_error("restore execute failed: " + diagnostic(restored_result));
    require_command({"btrfs", "subvolume", "show", restored.string()}, "inspect restored subvolume");
    require_command({"diff", "-qr", latest_remote.string(), restored.string()}, "compare restored subvolume");
    auto drill = restore_command("drill", drill_destination, "real-drill");
    if (drill.status != 0 || fs::exists(drill_destination) ||
        fs::exists(drill_root / ".btrfs-backup-restore-real-drill.staging"))
        throw std::runtime_error("restore drill failed or retained artifacts: " + diagnostic(drill));

    require_command({"btrfs", "subvolume", "delete", restored.string()}, "delete restored subvolume");
    const bool removed_drill_root = fs::remove(drill_root);
    const bool removed_catalog = fs::remove(repository / "catalog.json");
    const bool removed_repository = fs::remove(repository / "repository.json");
    if (!removed_drill_root || !removed_catalog || !removed_repository)
        throw std::runtime_error("public restore cleanup left artifacts");
}

void RealBtrfsTestEnvironment::require_public_cancellation() const {
    const fs::path profile_path = config_root_ / "profiles/raii/profile.json";
    std::ifstream profile_input(profile_path);
    Json profile = Json::parse(profile_input);
    profile_input.close();
    if (!profile_input)
        throw std::runtime_error("cannot read profile before cancellation test");
    const std::string original_profile = profile.dump() + "\n";
    const fs::path last_success = state_root_ / "profiles/raii/last-success";
    const auto read_document = [](const fs::path& path) {
        std::ifstream input(path);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    const std::string success_before = read_document(last_success);
    const std::size_t local_before = local_snapshot_count();
    const std::size_t remote_before = remote_snapshot_count();
    const fs::path hook = fs::path("/etc/btrfs-backup/hooks.d") /
        ("raii-cancel-" + root_.filename().string());
    if (fs::exists(hook))
        throw std::runtime_error("cancellation hook path already exists: " + hook.string());
    fs::copy_file(backupctl_, hook, fs::copy_options::none);
    fs::permissions(
        hook,
        fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
            fs::perms::others_read | fs::perms::others_exec,
        fs::perm_options::replace
    );
    const std::string run_id = "20260824T100000Z-raii-cancel";
    profile["hooks"]["beforeSnapshot"] = Json::array({{{"type", "program"}, {"program", hook.string()}, {"arguments", Json::array({"--profile-dir", config_root_.string(), "--status-root", status_root_.string(), "--history-root", history_root_.string(), "runner", "cancel", "--profile", "raii", "--run-id", run_id})}, {"timeoutSeconds", 30}}});
    write_file(profile_path, profile.dump() + "\n");

    CommandResult result;
    try {
        result = execute_backup("2026-08-24T10:00:00Z", run_id);
        write_file(profile_path, original_profile);
        if (!fs::remove(hook))
            throw std::runtime_error("cancellation hook disappeared before cleanup");
    } catch (...) {
        std::error_code ignored;
        fs::remove(hook, ignored);
        write_file(profile_path, original_profile);
        throw;
    }
    if (result.status != 1)
        throw std::runtime_error("cancelled runner returned an unexpected status: " + diagnostic(result));
    const Json response = Json::parse(result.output);
    if (response.at("completed").get<bool>() || !response.at("cancelled").get<bool>())
        throw std::runtime_error("runner did not report cancellation");
    std::ifstream status_input(status_root_ / "raii/current.json");
    const Json current = Json::parse(status_input);
    if (current.at("state") != "cancelled" || current.at("errorCode") != "backup.cancelled")
        throw std::runtime_error("cancelled runner persisted an invalid terminal status");
    if (fs::exists(state_root_ / "profiles/raii/cancel-request"))
        throw std::runtime_error("runner retained a handled cancellation request");
    if (read_document(last_success) != success_before || local_snapshot_count() != local_before ||
        remote_snapshot_count() != remote_before)
        throw std::runtime_error("cancelled runner changed successful snapshot state");
    if (fs::exists(state_root_ / "profiles/raii/pending-home") || !incoming_is_empty())
        throw std::runtime_error("cancelled pre-snapshot runner retained transactional artifacts");
}

void RealBtrfsTestEnvironment::require_browse_session() const {
    constexpr std::string_view user = "btrfs-raii-test";
    const std::string root_name = root_.filename().string();
    const std::string suffix = root_name.substr(root_name.size() - 6U);
    const std::string unit_stem = "btrfs-backup-raii-manager-" + suffix;
    const std::string unit = unit_stem + ".service";
    const fs::path policy = "/etc/polkit-1/rules.d/49-btrfs-backup-raii.rules";
    const fs::path probe = target_mount_ / "snapshots/browse-probe.txt";
    const fs::path manager = backupctl_.parent_path() / "btrfs-backupd";
    if (!fs::is_regular_file(manager) || fs::exists(policy) || fs::exists(probe))
        throw std::runtime_error("browse fixture prerequisites are unsafe");
    if (command({"id", "-u", std::string(user)}).status == 0)
        throw std::runtime_error("browse fixture user already exists");
    if (command({"busctl", "--system", "status", "io.github.btrfsbackup.Manager1"}).status == 0)
        throw std::runtime_error("manager D-Bus name is already owned");

    bool user_created = false;
    bool manager_started = false;
    const bool polkit_was_active =
        command({"systemctl", "is-active", "--quiet", "polkit.service"}).status == 0;
    const auto cleanup = [&] {
        std::vector<std::string> errors;
        const auto run_cleanup = [&](std::vector<std::string> arguments, std::string_view operation) {
            try {
                const auto result = command(std::move(arguments), std::chrono::seconds(30));
                if (result.status != 0)
                    errors.push_back(std::string(operation) + ": " + diagnostic(result));
            } catch (const std::exception& error) {
                errors.push_back(std::string(operation) + ": " + error.what());
            }
        };
        if (manager_started)
            run_cleanup({"systemctl", "stop", unit}, "stop isolated browse manager");
        std::error_code ignored;
        fs::remove(policy, ignored);
        if (ignored)
            errors.push_back("remove browse polkit rule: " + ignored.message());
        if (polkit_was_active)
            run_cleanup({"systemctl", "restart", "polkit.service"}, "reload original polkit rules");
        else
            run_cleanup({"systemctl", "stop", "polkit.service"}, "stop browse polkit authority");
        fs::remove(probe, ignored);
        if (ignored)
            errors.push_back("remove browse probe: " + ignored.message());
        if (user_created)
            run_cleanup({"userdel", std::string(user)}, "remove browse test user");
        return errors;
    };

    try {
        auto created = command(
            {"useradd", "--system", "--no-create-home", "--shell", "/usr/bin/nologin", std::string(user)}
        );
        if (created.status != 0)
            throw std::runtime_error("cannot create browse test user: " + diagnostic(created));
        user_created = true;
        const auto uid = command({"id", "-u", std::string(user)});
        if (uid.status != 0 || trim(uid.output) == "0")
            throw std::runtime_error("browse test caller is not an unprivileged user");
        write_file(
            policy,
            "polkit.addRule(function(action, subject) {\n"
            "  if (action.id == \"io.github.btrfsbackup.open-browse-session\" &&\n"
            "      subject.user == \"btrfs-raii-test\") return polkit.Result.YES;\n"
            "});\n"
        );
        require_command({"chmod", "0644", policy.string()}, "protect browse polkit rule");
        require_command({"systemctl", "restart", "polkit.service"}, "start browse polkit authority");
        fs::create_directories(root_ / "public-profiles");
        write_file(probe, "browse probe\n");
        auto started = command(
            {"systemd-run",
             "--unit=" + unit_stem,
             "--collect",
             "--property=Type=dbus",
             "--property=BusName=io.github.btrfsbackup.Manager1",
             manager.string(),
             "--config-root",
             config_root_.string(),
             "--public-profile-root",
             (root_ / "public-profiles").string(),
             "--status-root",
             status_root_.string(),
             "--history-root",
             history_root_.string(),
             "--target-mount-root",
             target_mount_root_.string(),
             "--audit-log",
             (root_ / "manager-audit.jsonl").string(),
             "--skip-configuration-activation"},
            std::chrono::seconds(30)
        );
        if (started.status != 0)
            throw std::runtime_error("cannot start isolated browse manager: " + diagnostic(started));
        manager_started = true;
        require_command({"systemctl", "is-active", "--quiet", unit}, "verify isolated browse manager");
        const auto client = command(
            {"runuser",
             "-u",
             std::string(user),
             "--",
             browse_session_client_.string(),
             "raii",
             "--self-check",
             target_mount_.string()},
            std::chrono::seconds(30)
        );
        if (client.status != 0)
            throw std::runtime_error("unprivileged browse session failed: " + diagnostic(client));
        require_command({"findmnt", "--mountpoint", target_mount_.string()}, "verify target after browse cleanup");
    } catch (const std::exception& error) {
        const std::string primary = error.what();
        const auto cleanup_errors = cleanup();
        if (!cleanup_errors.empty())
            throw std::runtime_error(primary + "; browse cleanup failed: " + join_errors(cleanup_errors));
        throw;
    } catch (...) {
        const auto cleanup_errors = cleanup();
        if (!cleanup_errors.empty())
            throw std::runtime_error("browse scenario and cleanup failed: " + join_errors(cleanup_errors));
        throw;
    }
    const auto cleanup_errors = cleanup();
    if (!cleanup_errors.empty())
        throw std::runtime_error("browse cleanup failed: " + join_errors(cleanup_errors));
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
