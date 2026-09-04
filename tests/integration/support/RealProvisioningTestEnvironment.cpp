// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealProvisioningTestEnvironment.hpp"

#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <utility>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;

RealProvisioningTestEnvironment::RealProvisioningTestEnvironment(fs::path client, fs::path source)
    : client_(fs::canonical(client)), source_backing_(fs::canonical(source)) {
    const char* consent = std::getenv("BTRFSBACKUP_REAL_BTRFS_CONTAINER");
    if (geteuid() != 0 || consent == nullptr || std::string_view(consent) != "1")
        throw std::runtime_error("real provisioning test requires root in its disposable container");
    const std::string base = (fs::temp_directory_path() / "btrfs-backup-provisioning.XXXXXX").string();
    std::vector<char> pattern(base.begin(), base.end());
    pattern.push_back('\0');
    const char* created = mkdtemp(pattern.data());
    if (created == nullptr)
        throw std::runtime_error("mkdtemp failed for provisioning fixture");
    root_ = created;
    source_ = fs::path("/mnt") / ("btrfs-backup-provisioning-source-" + root_.filename().string());
    image_ = root_ / "device.img";
    preserved_mount_ = root_ / "preserved";
    staging_mount_ = root_ / "staging";
    adoption_mapper_name_ = "bb-real-adoption-" + root_.filename().string();
    adoption_mapper_path_ = fs::path("/dev/mapper") / adoption_mapper_name_;
    write_test_file(root_ / ".btrfs-backup-test-root", "managed provisioning fixture\n");
    fs::create_directory(preserved_mount_);
    fs::create_directory(staging_mount_);
    fs::create_directory(source_);
    require_command({"mount", "--bind", source_backing_.string(), source_.string()}, "bind provisioning source");
    source_bind_mounted_ = true;
}

RealProvisioningTestEnvironment::~RealProvisioningTestEnvironment() noexcept {
    if (!closed_)
        static_cast<void>(release_resources());
}

CommandResult RealProvisioningTestEnvironment::command(
    std::vector<std::string> arguments,
    std::string_view standard_input
) const {
    return run_test_process(std::move(arguments), std::chrono::seconds(180), standard_input);
}

void RealProvisioningTestEnvironment::require_command(
    std::vector<std::string> arguments,
    std::string_view operation
) const {
    const auto result = command(std::move(arguments));
    if (result.status != 0)
        throw std::runtime_error(std::string(operation) + " failed: " + command_diagnostic(result));
}

void RealProvisioningTestEnvironment::require_block_device(
    const fs::path& path,
    std::string_view operation
) const {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto readable = command(
            {"dd", "if=" + path.string(), "of=/dev/null", "bs=512", "count=1", "status=none"}
        );
        if (fs::is_block_file(path) && readable.status == 0)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    throw std::runtime_error(std::string(operation) + ": " + path.string());
}

void RealProvisioningTestEnvironment::attach_image(std::string_view size) {
    if (!loop_.empty())
        throw std::runtime_error("provisioning fixture loop is already attached");
    require_command({"truncate", "-s", std::string(size), image_.string()}, "create provisioning image");
    const auto attached = command({"losetup", "--find", "--show", "--partscan", image_.string()});
    if (attached.status != 0)
        throw std::runtime_error("attach provisioning loop failed: " + command_diagnostic(attached));
    loop_ = trim_output(attached.output);
    const auto backing = command({"losetup", "--noheadings", "--output", "BACK-FILE", loop_});
    if (backing.status != 0 || trim_output(backing.output) != image_.string())
        throw std::runtime_error("provisioning loop ownership could not be verified");
}

void RealProvisioningTestEnvironment::start_manager() {
    if (manager_started_)
        return;
    require_command({"systemctl", "start", "polkit.service", "btrfs-backupd.service"}, "start manager");
    manager_started_ = true;
}

void RealProvisioningTestEnvironment::stop_manager() {
    if (!manager_started_)
        return;
    require_command({"systemctl", "stop", "btrfs-backupd.service", "polkit.service"}, "stop manager");
    manager_started_ = false;
}

std::string RealProvisioningTestEnvironment::provision(
    const fs::path& target,
    std::string_view mode,
    std::string_view profile_id
) const {
    const auto result = command(
        {client_.string(), target.string(), source_.string(), "-", std::string(mode), std::string(profile_id)},
        passphrase_
    );
    if (result.status != 0) {
        const auto manager_log = command(
            {"journalctl", "--no-pager", "-o", "cat", "-u", "btrfs-backupd.service", "-n", "100"}
        );
        const auto helper_log = command(
            {"journalctl", "--no-pager", "-o", "cat", "-t", "btrfs-backup-device-preparation", "-n", "100"}
        );
        throw std::runtime_error(
            "device provisioning client failed: " + command_diagnostic(result) +
            "\nmanager journal:\n" + command_diagnostic(manager_log) +
            "\nhelper journal:\n" + command_diagnostic(helper_log)
        );
    }
    return result.output;
}

void RealProvisioningTestEnvironment::delete_profile(std::string_view profile_id) const {
    const auto result = command({client_.string(), "--delete-profile", std::string(profile_id)});
    if (result.status != 0)
        throw std::runtime_error("delete provisioning profile failed: " + command_diagnostic(result));
}

std::vector<std::string> RealProvisioningTestEnvironment::release_resources() noexcept {
    std::vector<std::string> errors;
    const auto cleanup = [&](std::vector<std::string> arguments, std::string_view operation) {
        try {
            const auto result = run_test_process(std::move(arguments), std::chrono::seconds(30));
            if (result.status != 0)
                errors.push_back(std::string(operation) + ": " + command_diagnostic(result));
            return result.status == 0;
        } catch (const std::exception& error) {
            errors.push_back(std::string(operation) + ": " + error.what());
            return false;
        }
    };
    if (preserved_mounted_ && cleanup({"umount", preserved_mount_.string()}, "unmount preserved sibling"))
        preserved_mounted_ = false;
    if (staging_mounted_ && cleanup({"umount", staging_mount_.string()}, "unmount adoption staging"))
        staging_mounted_ = false;
    if (adoption_mapper_open_ && !staging_mounted_ &&
        cleanup({"cryptsetup", "close", adoption_mapper_name_}, "close adoption mapper"))
        adoption_mapper_open_ = false;
    if (manager_started_ &&
        cleanup({"systemctl", "stop", "btrfs-backupd.service", "polkit.service"}, "stop manager"))
        manager_started_ = false;
    if (!loop_.empty()) {
        try {
            const auto backing = run_test_process(
                {"losetup", "--noheadings", "--output", "BACK-FILE", loop_},
                std::chrono::seconds(10)
            );
            if (backing.status != 0 || trim_output(backing.output) != image_.string()) {
                errors.push_back("provisioning loop ownership changed; refusing detach");
            } else if (cleanup({"losetup", "-d", loop_}, "detach provisioning loop")) {
                loop_.clear();
            }
        } catch (const std::exception& error) {
            errors.push_back("verify provisioning loop ownership: " + std::string(error.what()));
        }
    }
    if (source_bind_mounted_ && cleanup({"umount", source_.string()}, "unmount provisioning source bind"))
        source_bind_mounted_ = false;
    if (!source_bind_mounted_ && fs::exists(source_)) {
        std::error_code error;
        if (!fs::remove(source_, error) || error)
            errors.push_back("remove provisioning source mount: " + error.message());
    }
    if (!preserved_mounted_ && !staging_mounted_ && !adoption_mapper_open_ && !manager_started_ &&
        loop_.empty() && !source_bind_mounted_) {
        if (!fs::is_regular_file(root_ / ".btrfs-backup-test-root")) {
            errors.push_back("provisioning root marker is missing; refusing removal");
        } else {
            std::error_code error;
            fs::remove_all(root_, error);
            if (error)
                errors.push_back("remove provisioning root: " + error.message());
        }
    }
    wipe_test_secret(passphrase_);
    closed_ = errors.empty();
    return errors;
}

void RealProvisioningTestEnvironment::close() {
    const auto errors = release_resources();
    if (!errors.empty())
        throw std::runtime_error("real provisioning cleanup failed: " + join_test_errors(errors));
}

} // namespace btrfsbackup::integration
