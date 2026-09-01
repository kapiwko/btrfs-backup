// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/filesystem/SecretFile.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <string>

#include <core/Errors.hpp>

#include "support/TestHelpers.hpp"

namespace {

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

} // namespace

int main() {
    test_secret_is_copied_and_sealed();
    test_secret_size_is_bounded();
    return test_helpers::finish("secret file tests");
}
