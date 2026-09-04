// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/IntegrationTestProcess.hpp"

#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;

class RealMapperLifecycleTest final {
  public:
    RealMapperLifecycleTest(
        fs::path target_mount,
        fs::path target_device,
        std::string mapper_name,
        fs::path passphrase_file
    )
        : target_mount_(fs::canonical(target_mount)),
          target_device_(fs::canonical(target_device)),
          mapper_name_(std::move(mapper_name)),
          mapper_path_(fs::path("/dev/mapper") / mapper_name_),
          passphrase_file_(fs::canonical(passphrase_file)) {
        const char* consent = std::getenv("BTRFSBACKUP_REAL_BTRFS_CONTAINER");
        if (geteuid() != 0 || consent == nullptr || std::string_view(consent) != "1")
            throw std::runtime_error("real mapper lifecycle test requires root in its disposable container");
        if (mapper_name_.empty() || mapper_name_.contains('/'))
            throw std::runtime_error("invalid integration mapper name");
    }

    void close_and_reopen() const {
        require_success({"umount", target_mount_.string()}, "unmount target");
        require_success({"cryptsetup", "close", mapper_name_}, "close target mapper");
        wait_for_closed_mapper();
        require_success(
            {"cryptsetup", "open", "--key-file", passphrase_file_.string(), target_device_.string(), mapper_name_},
            "reopen target mapper"
        );
        require_success({"udevadm", "settle", "--timeout=10"}, "settle reopened target mapper");
        require_success({"dmsetup", "mknodes", mapper_name_}, "materialize reopened target mapper");
        if (!fs::is_block_file(mapper_path_))
            throw std::runtime_error("reopened target mapper node is missing");
        require_success(
            {"mount", "-o", "noatime,nodev,nosuid,noexec,nosymfollow,compress=zstd:3", mapper_path_.string(), target_mount_.string()},
            "remount target"
        );
    }

  private:
    [[nodiscard]] CommandResult command(std::vector<std::string> arguments) const {
        return run_test_process(std::move(arguments), std::chrono::seconds(30));
    }

    void require_success(std::vector<std::string> arguments, std::string_view operation) const {
        const auto result = command(std::move(arguments));
        if (result.status != 0)
            throw std::runtime_error(std::string(operation) + " failed: " + command_diagnostic(result));
    }

    void wait_for_closed_mapper() const {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            if (command({"cryptsetup", "status", mapper_name_}).status != 0 && !fs::exists(mapper_path_))
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        throw std::runtime_error("target mapper remained active after close");
    }

    fs::path target_mount_;
    fs::path target_device_;
    std::string mapper_name_;
    fs::path mapper_path_;
    fs::path passphrase_file_;
};

} // namespace btrfsbackup::integration

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: btrfsbackup-real-mapper-lifecycle-tests "
                     "TARGET_MOUNT TARGET_DEVICE MAPPER_NAME PASSPHRASE_FILE\n";
        return 2;
    }
    try {
        btrfsbackup::integration::RealMapperLifecycleTest(argv[1], argv[2], argv[3], argv[4]).close_and_reopen();
        std::cout << "ok - plain unmount closes and reopens the test mapper\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "real-mapper-lifecycle-tests: " << error.what() << '\n';
        return 1;
    }
}
