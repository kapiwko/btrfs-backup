// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
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
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct CommandResult {
    int status{};
    std::string output;
    bool termination_requested{false};
};

[[nodiscard]] std::vector<char*> argument_pointers(std::vector<std::string>& arguments) {
    std::vector<char*> pointers;
    pointers.reserve(arguments.size() + 1U);
    for (auto& argument : arguments)
        pointers.push_back(argument.data());
    pointers.push_back(nullptr);
    return pointers;
}

[[nodiscard]] CommandResult run_command(
    std::vector<std::string> arguments,
    std::chrono::seconds timeout = std::chrono::seconds(30),
    std::optional<fs::path> terminate_when_exists = std::nullopt
) {
    constexpr std::size_t maximum_output_bytes = 1024U * 1024U;
    std::array<int, 2> descriptors{};
    if (pipe2(descriptors.data(), O_CLOEXEC) != 0)
        throw std::runtime_error("pipe failed");
    const pid_t child = fork();
    if (child < 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        throw std::runtime_error("fork failed");
    }
    if (child == 0) {
        close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0 || dup2(descriptors[1], STDERR_FILENO) < 0)
            _exit(127);
        close(descriptors[1]);
        if (clearenv() != 0 || setenv("PATH", "/usr/bin:/usr/sbin", 1) != 0 ||
            setenv("LANG", "C.UTF-8", 1) != 0 || setenv("LC_ALL", "C.UTF-8", 1) != 0 ||
            setenv("HOME", "/root", 1) != 0)
            _exit(127);
        auto pointers = argument_pointers(arguments);
        execvp(pointers.front(), pointers.data());
        _exit(127);
    }

    close(descriptors[1]);
    const int flags = fcntl(descriptors[0], F_GETFL, 0);
    if (flags < 0 || fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) < 0) {
        kill(child, SIGKILL);
        while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
        close(descriptors[0]);
        throw std::runtime_error("cannot configure command output pipe");
    }
    std::string output;
    int status = 0;
    bool exited = false;
    bool termination_requested = false;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        std::array<char, 4096> buffer{};
        const ssize_t count = read(descriptors[0], buffer.data(), buffer.size());
        if (count > 0) {
            if (output.size() + static_cast<std::size_t>(count) > maximum_output_bytes) {
                kill(child, SIGKILL);
                while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
                close(descriptors[0]);
                throw std::runtime_error("command output exceeded 1 MiB: " + arguments.front());
            }
            output.append(buffer.data(), static_cast<std::size_t>(count));
        }
        if (!termination_requested && terminate_when_exists.has_value() &&
            fs::exists(*terminate_when_exists)) {
            if (kill(child, SIGTERM) != 0 && errno != ESRCH) {
                kill(child, SIGKILL);
                while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
                close(descriptors[0]);
                throw std::runtime_error("cannot request command termination: " + arguments.front());
            }
            termination_requested = true;
        }
        const pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            exited = true;
            break;
        }
        if (result < 0 && errno != EINTR)
            break;
        pollfd descriptor{descriptors[0], POLLIN, 0};
        static_cast<void>(poll(&descriptor, 1, 10));
    }
    if (!exited) {
        kill(child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        close(descriptors[0]);
        throw std::runtime_error("command timed out: " + arguments.front());
    }
    std::array<char, 4096> buffer{};
    for (;;) {
        const ssize_t count = read(descriptors[0], buffer.data(), buffer.size());
        if (count <= 0)
            break;
        if (output.size() + static_cast<std::size_t>(count) > maximum_output_bytes) {
            close(descriptors[0]);
            throw std::runtime_error("command output exceeded 1 MiB: " + arguments.front());
        }
        output.append(buffer.data(), static_cast<std::size_t>(count));
    }
    close(descriptors[0]);
    return {
        WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status),
        std::move(output),
        termination_requested,
    };
}

[[nodiscard]] std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    const auto last = value.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : value.substr(first, last - first + 1U);
}

void require(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void require_success(const CommandResult& result, std::string_view operation) {
    if (result.status != 0)
        throw std::runtime_error(std::string(operation) + " failed: " + result.output);
}

void write_file(const fs::path& path, std::string_view content) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    output << content;
    output.close();
    if (!output)
        throw std::runtime_error("cannot write " + path.string());
}

class Fixture final {
  public:
    explicit Fixture(const fs::path& ctl) : ctl_(fs::canonical(ctl)) {
        const std::string base = (fs::temp_directory_path() / "btrfs-backup-restore-real.XXXXXX").string();
        std::vector<char> pattern(base.begin(), base.end());
        pattern.push_back('\0');
        const char* created = mkdtemp(pattern.data());
        if (created == nullptr)
            throw std::runtime_error("mkdtemp failed");
        root_ = created;
        image_ = root_ / "filesystem.img";
        mount_ = root_ / "mount";
        write_file(root_ / ".btrfs-backup-test-root", "managed restore fixture\n");
    }

    ~Fixture() noexcept {
        if (!closed_)
            static_cast<void>(release_resources());
    }

    void run() {
        require(geteuid() == 0, "real restore engine test must run as root");
        prepare_filesystem();
        create_repository();
        verify_plan_and_restore();
        verify_conflicts();
        verify_drill_cleanup();
        verify_cancellation();
    }

    void close() {
        const std::string cleanup_error = release_resources();
        if (!cleanup_error.empty())
            throw std::runtime_error("real restore cleanup failed: " + cleanup_error);
    }

  private:
    [[nodiscard]] CommandResult run_cleanup(std::vector<std::string> arguments) const noexcept {
        try {
            return run_command(std::move(arguments), std::chrono::seconds(10));
        } catch (...) {
            return {1, {}, false};
        }
    }

    [[nodiscard]] CommandResult ctl(std::vector<std::string> arguments) const {
        arguments.insert(arguments.begin(), ctl_.string());
        return run_command(std::move(arguments));
    }

    [[nodiscard]] CommandResult ctl(
        std::vector<std::string> arguments,
        const fs::path& terminate_when_exists
    ) const {
        arguments.insert(arguments.begin(), ctl_.string());
        return run_command(std::move(arguments), std::chrono::seconds(30), terminate_when_exists);
    }

    void prepare_filesystem() {
        require_success(run_command({"truncate", "-s", "512M", image_.string()}), "truncate image");
        const auto attached = run_command({"losetup", "--find", "--show", image_.string()});
        require_success(attached, "attach loop device");
        loop_ = trim(attached.output);
        require(!loop_.empty(), "losetup returned no device");
        verify_owned_loop();
        require_success(run_command({"mkfs.btrfs", "-q", "-f", loop_}), "create Btrfs filesystem");
        fs::create_directories(mount_);
        require_success(run_command({"mount", "-o", "noatime", loop_, mount_.string()}), "mount Btrfs filesystem");
        mounted_ = true;
    }

    void create_repository() {
        require_success(run_command({"btrfs", "subvolume", "create", (mount_ / "source").string()}), "create source subvolume");
        fs::create_directories(mount_ / "source/Documents");
        fs::create_directories(mount_ / "repository/hosts/host/profiles/default/sources/home");
        const fs::path report = mount_ / "source/Documents/report.txt";
        write_file(report, "restore engine\n");
        require_success(
            run_command({"dd", "if=/dev/urandom", "of=" + (mount_ / "source/Documents/large.bin").string(), "bs=1M", "count=96", "status=none"}),
            "create cancellation payload"
        );
        require(chmod(report.c_str(), 0640) == 0, "cannot set source metadata");
        snapshot_ = mount_ / "repository/hosts/host/profiles/default/sources/home/snapshot";
        require_success(run_command({"btrfs", "subvolume", "snapshot", "-r", (mount_ / "source").string(), snapshot_.string()}), "create read-only snapshot");
        const auto metadata = run_command({"btrfs", "subvolume", "show", snapshot_.string()});
        require_success(metadata, "read snapshot metadata");
        const std::string marker = "UUID:";
        const auto position = metadata.output.find(marker);
        require(position != std::string::npos, "snapshot metadata omitted UUID");
        const auto end = metadata.output.find('\n', position);
        const std::string uuid = trim(metadata.output.substr(position + marker.size(), end - position - marker.size()));
        require(!uuid.empty(), "snapshot UUID is empty");
        const fs::path repository = mount_ / "repository";
        write_file(repository / "repository.json", R"({"schemaVersion":1,"repositoryId":"real-restore","targetFilesystemUuid":"test-filesystem","createdAt":"2026-01-01T00:00:00Z","features":["catalog-v1"]})"
                                                   "\n");
        write_file(repository / "catalog.json", R"({"schemaVersion":1,"generation":1,"snapshots":[{"snapshotId":"snapshot","hostId":"host","profileId":"default","sourceId":"home","relativePath":"hosts/host/profiles/default/sources/home/snapshot","createdAt":"2026-01-01T00:00:00Z","uuid":")" + uuid + R"(","verified":true}]})" + "\n");
    }

    [[nodiscard]] std::vector<std::string> restore_arguments(
        std::string command,
        const fs::path& destination,
        std::string transaction,
        bool subvolume
    ) const {
        std::vector<std::string> arguments{
            "restore",
            std::move(command),
            "--repository",
            (mount_ / "repository").string(),
            "--snapshot",
            "snapshot",
            "--source",
            ".",
            "--destination",
            destination.string(),
            "--transaction",
            std::move(transaction)
        };
        if (subvolume)
            arguments.push_back("--subvolume");
        return arguments;
    }

    void verify_plan_and_restore() {
        const fs::path restored = mount_ / "restored";
        require_success(ctl(restore_arguments("plan", restored, "real-plan", true)), "plan restore");
        require(!fs::exists(restored), "restore plan modified the destination");
        require_success(ctl(restore_arguments("execute", restored, "real-execute", true)), "execute restore");
        require_success(run_command({"btrfs", "subvolume", "show", restored.string()}), "validate restored subvolume");
        require_success(run_command({"diff", "-qr", snapshot_.string(), restored.string()}), "compare restored subvolume");
        struct stat report_stat{};
        require(stat((restored / "Documents/report.txt").c_str(), &report_stat) == 0, "restored report is missing");
        require((report_stat.st_mode & 0777) == 0640, "restore did not preserve file permissions");
    }

    void verify_conflicts() {
        const fs::path restored = mount_ / "restored";
        require(ctl(restore_arguments("execute", restored, "destination-conflict", true)).status != 0, "restore overwrote an existing destination without --replace");
        require_success(run_command({"diff", "-qr", snapshot_.string(), restored.string()}), "destination changed after conflict");

        const fs::path blocked_destination = mount_ / "blocked";
        const fs::path staging = mount_ / ".btrfs-backup-restore-artifact-conflict.staging";
        fs::create_directories(staging);
        write_file(staging / "owner-marker", "foreign\n");
        require(ctl(restore_arguments("execute", blocked_destination, "artifact-conflict", true)).status != 0, "restore accepted a pre-existing transaction artifact");
        require(fs::exists(staging / "owner-marker"), "restore removed a transaction artifact it did not create");
        require(!fs::exists(blocked_destination), "failed restore committed a destination");
        fs::remove_all(staging);
    }

    void verify_drill_cleanup() {
        auto arguments = restore_arguments("drill", mount_ / "drill/result", "real-drill", false);
        const auto source = std::find(arguments.begin(), arguments.end(), "--source");
        require(source != arguments.end(), "internal restore arguments omit source");
        *(source + 1) = "Documents";
        require_success(ctl(std::move(arguments)), "execute restore drill");
        require(!fs::exists(mount_ / "drill/result"), "restore drill committed a destination");
        require(!fs::exists(mount_ / "drill/.btrfs-backup-restore-real-drill.staging"), "restore drill left staging data");
    }

    void verify_cancellation() {
        const fs::path destination = mount_ / "cancelled";
        const fs::path staging = mount_ / ".btrfs-backup-restore-real-cancel.staging";
        const auto result = ctl(
            restore_arguments("execute", destination, "real-cancel", false),
            staging
        );
        require(result.termination_requested, "restore completed before cancellation could be requested");
        require(result.status != 0, "cancelled restore reported success");
        require(!fs::exists(destination), "cancelled restore committed its destination");
        require(!fs::exists(staging), "cancelled restore left staging data");
    }

    void verify_owned_loop() const {
        struct stat status{};
        require(
            stat(loop_.c_str(), &status) == 0 && S_ISBLK(status.st_mode) && major(status.st_rdev) == 7U,
            "losetup returned an invalid loop device"
        );
        const auto backing = run_command({"losetup", "--noheadings", "--output", "BACK-FILE", loop_});
        require_success(backing, "inspect loop backing file");
        require(trim(backing.output) == image_.string(), "loop device is not owned by the restore fixture");
        std::cout << "artifacts - root=" << root_ << " image=" << image_ << " loop=" << loop_
                  << " mount=" << mount_ << '\n';
    }

    [[nodiscard]] std::string release_resources() noexcept {
        std::string errors;
        const auto record = [&errors](std::string_view operation, const CommandResult& result) {
            if (result.status == 0)
                return;
            if (!errors.empty())
                errors += "; ";
            errors += std::string(operation) + ": " + result.output;
        };
        if (mounted_) {
            const auto result = run_cleanup({"umount", mount_.string()});
            record("unmount", result);
            if (result.status == 0)
                mounted_ = false;
        }
        if (!loop_.empty() && !mounted_) {
            const auto backing = run_cleanup({"losetup", "--noheadings", "--output", "BACK-FILE", loop_});
            if (backing.status != 0) {
                loop_.clear();
            } else if (trim(backing.output) != image_.string()) {
                record("loop ownership changed; refusing detach", {1, loop_, false});
            } else {
                const auto result = run_cleanup({"losetup", "-d", loop_});
                record("detach loop", result);
                if (result.status == 0)
                    loop_.clear();
            }
        }
        if (!mounted_ && loop_.empty()) {
            if (!fs::is_regular_file(root_ / ".btrfs-backup-test-root")) {
                record("test-root marker missing; refusing removal", {1, root_.string(), false});
            } else {
                std::error_code error;
                fs::remove_all(root_, error);
                if (error)
                    record("remove test root", {1, error.message(), false});
            }
        }
        closed_ = errors.empty();
        return errors;
    }

    fs::path ctl_;
    fs::path root_;
    fs::path image_;
    fs::path mount_;
    fs::path snapshot_;
    std::string loop_;
    bool mounted_{false};
    bool closed_{false};
};

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: btrfsbackup-real-restore-tests /path/to/btrfs-backupctl\n";
        return 2;
    }
    try {
        Fixture fixture(argv[1]);
        fixture.run();
        fixture.close();
        std::cout << "ok - real Btrfs restore engine test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "real-restore-engine-tests: " << error.what() << '\n';
        return 1;
    }
}
