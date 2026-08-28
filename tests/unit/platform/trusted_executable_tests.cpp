// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <string>

#include <platform/linux/process.hpp>
#include <platform/linux/trusted_executable.hpp>

#include "support/validation_test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

constexpr btrfsbackup::backup::TrustedExecutablePolicy rootless_test_policy{
    .allow_current_user_owner = true,
    .verify_parent_directories = false,
};

fs::path write_executable(const fs::path& path, const std::string& output) {
    test_helpers::write_file(path, "#!/bin/sh\nprintf '%s\\n' '" + output + "'\n");
    chmod(path.c_str(), 0700);
    return path;
}

void test_accepts_private_regular_executable() {
    fs::path root = test_helpers::test_root("trusted-executable", "valid");
    fs::path program = write_executable(root / "prepare", "valid");
    btrfsbackup::platform::linux::SafeDirectoryRoot trusted_root(root);

    btrfsbackup::platform::linux::SafeDirectoryHandle executable = btrfsbackup::platform::linux::open_trusted_executable(
        trusted_root,
        program,
        rootless_test_policy
    );

    test_helpers::expect_true("trusted executable descriptor", executable.fd() >= 3, "descriptor is not open");
    fs::remove_all(root);
}

void test_rejects_symlink_and_non_regular_file() {
    fs::path root = test_helpers::test_root("trusted-executable", "file-type");
    fs::path real_program = write_executable(root / "real", "real");
    fs::create_symlink(real_program.filename(), root / "linked");
    fs::create_directory(root / "directory");
    btrfsbackup::platform::linux::SafeDirectoryRoot trusted_root(root);

    test_helpers::expect_validation_error("trusted executable symlink", [&] { (void)btrfsbackup::platform::linux::open_trusted_executable(trusted_root, root / "linked", rootless_test_policy); }, "cannot open path below safe directory root");
    test_helpers::expect_validation_error("trusted executable directory", [&] { (void)btrfsbackup::platform::linux::open_trusted_executable(trusted_root, root / "directory", rootless_test_policy); }, "not a regular file");

    fs::remove_all(root);
}

void test_rejects_unsafe_permissions_and_missing_execute_bit() {
    fs::path root = test_helpers::test_root("trusted-executable", "mode");
    fs::path program = write_executable(root / "prepare", "mode");
    btrfsbackup::platform::linux::SafeDirectoryRoot trusted_root(root);

    chmod(program.c_str(), 0720);
    test_helpers::expect_validation_error("trusted executable writable by group", [&] { (void)btrfsbackup::platform::linux::open_trusted_executable(trusted_root, program, rootless_test_policy); }, "writable by group or others");

    chmod(program.c_str(), 0600);
    test_helpers::expect_validation_error("trusted executable not executable", [&] { (void)btrfsbackup::platform::linux::open_trusted_executable(trusted_root, program, rootless_test_policy); }, "not executable");

    fs::remove_all(root);
}

void test_rejects_nested_program_and_untrusted_parent() {
    fs::path root = test_helpers::test_root("trusted-executable", "parent");
    fs::path nested = write_executable(root / "nested" / "prepare", "nested");
    btrfsbackup::platform::linux::SafeDirectoryRoot trusted_root(root);

    test_helpers::expect_validation_error("trusted executable nested", [&] { (void)btrfsbackup::platform::linux::open_trusted_executable(trusted_root, nested, rootless_test_policy); }, "must be a direct child");
    test_helpers::expect_validation_error("trusted executable writable parent", [&] { (void)btrfsbackup::platform::linux::open_trusted_executable(
                                                                                          trusted_root,
                                                                                          root / "nested",
                                                                                          {.allow_current_user_owner = true, .verify_parent_directories = true}
                                                                                      ); }, "trusted hook parent");

    fs::remove_all(root);
}

void test_pinned_descriptor_prevents_path_replacement_race() {
    fs::path root = test_helpers::test_root("trusted-executable", "pinned");
    fs::path program = write_executable(root / "prepare", "trusted-original");
    btrfsbackup::platform::linux::SafeDirectoryRoot trusted_root(root);
    btrfsbackup::platform::linux::SafeDirectoryHandle executable = btrfsbackup::platform::linux::open_trusted_executable(
        trusted_root,
        program,
        rootless_test_policy
    );

    fs::rename(program, root / "prepare.original");
    write_executable(program, "replacement");

    btrfsbackup::backup::CommandResult result = btrfsbackup::platform::linux::run_controlled_command(
        {executable.proc_path().string()},
        {
            .timeout = std::chrono::seconds(2),
            .inherited_fds = {executable.fd()},
        }
    );

    test_helpers::expect_eq("pinned executable exit", std::to_string(result.exit_code), "0");
    test_helpers::expect_contains("pinned executable output", result.output, "trusted-original");
    test_helpers::expect_true(
        "replacement not executed",
        result.output.find("replacement") == std::string::npos,
        "replacement path was executed instead of the pinned file"
    );

    fs::remove_all(root);
}

} // namespace

int main() {
    test_accepts_private_regular_executable();
    test_rejects_symlink_and_non_regular_file();
    test_rejects_unsafe_permissions_and_missing_execute_bit();
    test_rejects_nested_program_and_untrusted_parent();
    test_pinned_descriptor_prevents_path_replacement_race();

    return test_helpers::finish("trusted executable tests");
}
