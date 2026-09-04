// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealTrustedHookTest.hpp"

#include "IntegrationTestProcess.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

constexpr auto command_timeout = std::chrono::seconds(120);

[[nodiscard]] std::string read_file(const fs::path& path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open " + path.string());
    const std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad())
        throw std::runtime_error("cannot read " + path.string());
    return content;
}

[[nodiscard]] bool trusted_directory(const fs::path& path) {
    struct stat status{};
    return lstat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode) && status.st_uid == 0 &&
        status.st_gid == 0 && (status.st_mode & 0777) == 0755;
}

} // namespace

RealTrustedHookTest::RealTrustedHookTest(fs::path runtime, fs::path profile, fs::path test_root)
    : runtime_(fs::canonical(runtime)),
      profile_(fs::canonical(profile)),
      hook_(hook_directory_ / "integration-test"),
      original_hook_(hook_directory_ / "integration-test.original"),
      outside_hook_(fs::canonical(test_root) / "untrusted-hook"),
      marker_(fs::canonical(test_root) / "trusted-hook-ran") {
    const char* consent = std::getenv("BTRFSBACKUP_REAL_BTRFS_CONTAINER");
    if (geteuid() != 0 || consent == nullptr || std::string_view(consent) != "1")
        throw std::runtime_error("real trusted-hook test requires root in its disposable container");
    if (!trusted_directory(hook_directory_))
        throw std::runtime_error("package did not install the hook directory as root:root 0755");
    if (fs::exists(hook_) || fs::exists(original_hook_) || fs::exists(outside_hook_) || fs::exists(marker_))
        throw std::runtime_error("trusted-hook fixture path already exists");
    original_profile_ = read_file(profile_);
}

RealTrustedHookTest::~RealTrustedHookTest() noexcept {
    if (!closed_)
        static_cast<void>(release_resources());
}

void RealTrustedHookTest::require_success(
    std::vector<std::string> arguments,
    std::string_view operation
) const {
    const auto result = run_test_process(std::move(arguments), command_timeout);
    if (result.status != 0)
        throw std::runtime_error(std::string(operation) + " failed: " + command_diagnostic(result));
}

void RealTrustedHookTest::configure_hook() {
    Json profile = Json::parse(original_profile_);
    profile["hooks"]["beforeSnapshot"] = Json::array(
        {{{"type", "program"},
          {"program", hook_.string()},
          {"arguments", Json::array()},
          {"timeoutSeconds", 30}}}
    );
    write_test_file(profile_, profile.dump(2) + "\n");
    profile_modified_ = true;
    if (chmod(profile_.c_str(), 0600) != 0)
        throw std::runtime_error("cannot protect modified integration profile");
}

void RealTrustedHookTest::expect_backup_failure(std::string_view expected) const {
    const auto result = run_test_process(
        {"env", "INVOCATION_ID=real-docker-test", runtime_.string(), "--force", "--no-eject"},
        command_timeout
    );
    const std::string diagnostic = command_diagnostic(result);
    if (result.status == 0)
        throw std::runtime_error("backup unexpectedly succeeded; expected: " + std::string(expected));
    if (!diagnostic.contains(expected))
        throw std::runtime_error("backup failed without expected message: " + std::string(expected) + "\n" + diagnostic);
}

void RealTrustedHookTest::run() {
    configure_hook();
    write_test_file(hook_, "#!/bin/sh\nprintf 'trusted\\n' > '" + marker_.string() + "'\n");
    if (chmod(hook_.c_str(), 0755) != 0 || chown(hook_.c_str(), 0, 0) != 0)
        throw std::runtime_error("cannot prepare trusted integration hook");
    require_success(
        {"env", "INVOCATION_ID=real-docker-test", runtime_.string(), "--force", "--no-eject"},
        "execute trusted hook backup"
    );
    if (read_file(marker_) != "trusted\n")
        throw std::runtime_error("trusted root-owned hook was not executed");

    if (chown(hook_.c_str(), 1000, 1000) != 0)
        throw std::runtime_error("cannot set unsafe hook owner");
    expect_backup_failure("hook program must be owned by root");
    if (chown(hook_.c_str(), 0, 0) != 0)
        throw std::runtime_error("cannot restore trusted hook owner");

    if (chmod(hook_.c_str(), 0775) != 0)
        throw std::runtime_error("cannot set unsafe hook mode");
    expect_backup_failure("hook program must not be writable by group or others");
    if (chmod(hook_.c_str(), 0755) != 0)
        throw std::runtime_error("cannot restore trusted hook mode");

    if (chmod(hook_directory_.c_str(), 0777) != 0)
        throw std::runtime_error("cannot set unsafe hook directory mode");
    hook_directory_permissions_modified_ = true;
    expect_backup_failure("trusted hook parent must not be writable by group or others");
    if (chmod(hook_directory_.c_str(), 0755) != 0)
        throw std::runtime_error("cannot restore trusted hook directory mode");
    hook_directory_permissions_modified_ = false;

    fs::copy_file(hook_, outside_hook_);
    fs::rename(hook_, original_hook_);
    fs::create_symlink(outside_hook_, hook_);
    expect_backup_failure("Too many levels of symbolic links");
    fs::remove(hook_);
    fs::rename(original_hook_, hook_);
}

std::vector<std::string> RealTrustedHookTest::release_resources() noexcept {
    std::vector<std::string> errors;
    if (hook_directory_permissions_modified_ && chmod(hook_directory_.c_str(), 0755) != 0)
        errors.emplace_back("restore hook directory permissions");
    hook_directory_permissions_modified_ = false;
    if (profile_modified_) {
        try {
            write_test_file(profile_, original_profile_);
            if (chmod(profile_.c_str(), 0600) != 0)
                errors.emplace_back("restore profile permissions");
            else
                profile_modified_ = false;
        } catch (const std::exception& error) {
            errors.push_back("restore profile: " + std::string(error.what()));
        }
    }
    for (const auto& path : {hook_, original_hook_, outside_hook_, marker_}) {
        std::error_code error;
        static_cast<void>(fs::remove(path, error));
        if (error)
            errors.push_back("remove " + path.string() + ": " + error.message());
    }
    return errors;
}

void RealTrustedHookTest::close() {
    const auto errors = release_resources();
    if (!errors.empty())
        throw std::runtime_error("trusted-hook cleanup failed: " + join_test_errors(errors));
    closed_ = true;
}

} // namespace btrfsbackup::integration
