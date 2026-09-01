// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/filesystem/SecretFile.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <core/Errors.hpp>

#include "support/TestHelpers.hpp"

namespace {

namespace fs = std::filesystem;
namespace filesystem = btrfsbackup::platform::linux::filesystem;

btrfsbackup::platform::linux::OwnedFileDescriptor secret(std::string_view value) {
    return filesystem::create_sealed_secret_file(
        std::as_bytes(std::span(value.data(), value.size()))
    );
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void test_secret_is_copied_and_sealed() {
    int descriptors[2];
    test_helpers::expect_true("secret pipe", pipe(descriptors) == 0, "cannot create pipe");
    constexpr std::string_view secret = "correct horse battery staple";
    test_helpers::expect_true(
        "secret write",
        write(descriptors[1], secret.data(), secret.size()) == static_cast<ssize_t>(secret.size()),
        "cannot write secret"
    );
    close(descriptors[1]);

    auto protected_secret =
        btrfsbackup::platform::linux::filesystem::copy_secret_to_sealed_file(descriptors[0]);
    close(descriptors[0]);
    std::array<char, 64> output{};
    const ssize_t size = read(protected_secret.get(), output.data(), output.size());
    test_helpers::expect_eq(
        "protected secret",
        std::string(output.data(), static_cast<std::size_t>(size)),
        std::string(secret)
    );
    const int seals = fcntl(protected_secret.get(), F_GET_SEALS);
    test_helpers::expect_true(
        "protected secret seals",
        (seals & (F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)) ==
            (F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE),
        "secret file is mutable"
    );
}

void test_secret_size_is_bounded() {
    int descriptors[2];
    test_helpers::expect_true("large secret pipe", pipe(descriptors) == 0, "cannot create pipe");
    const std::string secret(32, 'x');
    static_cast<void>(write(descriptors[1], secret.data(), secret.size()));
    close(descriptors[1]);
    try {
        static_cast<void>(btrfsbackup::platform::linux::filesystem::copy_secret_to_sealed_file(
            descriptors[0],
            16
        ));
        test_helpers::fail("large secret", "oversized secret was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
    close(descriptors[0]);
}

void test_secret_is_installed_relative_to_trusted_directory() {
    const fs::path root = test_helpers::test_root("secret-file", "trusted-install");
    chmod(root.c_str(), 0700);
    const fs::path key_root = root / "keys";
    filesystem::ensure_trusted_directory(key_root, 0700, root, geteuid());
    filesystem::TrustedDirectory directory(key_root, root, geteuid());
    const filesystem::SafeFilename filename("profile-slot-1.key");
    auto source = secret("installed secret");

    filesystem::install_secret_file(directory, filename, source.get());

    test_helpers::expect_eq(
        "installed secret content",
        read_file(key_root / filename.value()),
        std::string("installed secret")
    );
    struct stat status{};
    test_helpers::expect_true(
        "installed secret mode",
        ::stat((key_root / filename.value()).c_str(), &status) == 0 && (status.st_mode & 0777) == 0600,
        "installed secret is not mode 0600"
    );
}

void test_install_does_not_replace_existing_entry() {
    const fs::path root = test_helpers::test_root("secret-file", "no-replace");
    chmod(root.c_str(), 0700);
    const fs::path key_root = root / "keys";
    filesystem::ensure_trusted_directory(key_root, 0700, root, geteuid());
    filesystem::TrustedDirectory directory(key_root, root, geteuid());
    const filesystem::SafeFilename filename("profile-slot-1.key");
    test_helpers::write_file(key_root / filename.value(), "existing secret");
    auto source = secret("replacement secret");

    try {
        filesystem::install_secret_file(directory, filename, source.get());
        test_helpers::fail("secret no-replace", "existing destination was replaced");
    } catch (const btrfsbackup::ValidationError&) {
    }

    test_helpers::expect_eq(
        "existing secret preserved",
        read_file(key_root / filename.value()),
        std::string("existing secret")
    );
}

void test_install_does_not_replace_symlink() {
    const fs::path root = test_helpers::test_root("secret-file", "symlink-no-replace");
    chmod(root.c_str(), 0700);
    const fs::path key_root = root / "keys";
    filesystem::ensure_trusted_directory(key_root, 0700, root, geteuid());
    filesystem::TrustedDirectory directory(key_root, root, geteuid());
    const filesystem::SafeFilename filename("profile-slot-1.key");
    const fs::path victim = root / "victim";
    test_helpers::write_file(victim, "victim content");
    fs::create_symlink(victim, key_root / filename.value());
    auto source = secret("replacement secret");

    try {
        filesystem::install_secret_file(directory, filename, source.get());
        test_helpers::fail("secret symlink no-replace", "symlink destination was replaced");
    } catch (const btrfsbackup::ValidationError&) {
    }

    test_helpers::expect_true(
        "secret symlink preserved",
        fs::is_symlink(key_root / filename.value()),
        "destination symlink was replaced"
    );
    test_helpers::expect_eq(
        "symlink victim preserved",
        read_file(victim),
        std::string("victim content")
    );
}

} // namespace

int main() {
    test_secret_is_copied_and_sealed();
    test_secret_size_is_bounded();
    test_secret_is_installed_relative_to_trusted_directory();
    test_install_does_not_replace_existing_entry();
    test_install_does_not_replace_symlink();
    return test_helpers::finish("secret file tests");
}
