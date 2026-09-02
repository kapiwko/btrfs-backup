// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/filesystem/SecretFile.hpp>
#include <platform/linux/storage/BlockDeviceMetadata.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>

#include <core/Errors.hpp>

#include "support/TestHelpers.hpp"

namespace {

template <typename Operation>
void expect_validation_error(const char* name, Operation operation) {
    try {
        operation();
        test_helpers::fail(name, "invalid LUKS operation was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
}

void test_rejects_untrusted_inputs_before_libcryptsetup() {
    btrfsbackup::platform::linux::storage::CryptsetupOperations operations;
    expect_validation_error("relative LUKS path", [&] {
        static_cast<void>(operations.inspect_luks2("dev/test"));
    });
    expect_validation_error("invalid mapper open", [&] {
        operations.open_luks2("/dev/null", "../mapper", 3);
    });
    expect_validation_error("invalid mapper inspection", [&] {
        static_cast<void>(operations.active_device("mapper/name"));
    });
    expect_validation_error("invalid mapper close", [&] {
        operations.close("mapper/name");
    });
}

void test_formats_and_manages_a_luks2_header_without_a_process() {
    namespace fs = std::filesystem;
    constexpr std::string_view passphrase = "correct horse battery staple";
    const fs::path root = test_helpers::test_root("cryptsetup-operations", "luks2-header");
    const fs::path image = root / "target.img";
    test_helpers::write_file(image, "");
    fs::resize_file(image, 64ULL * 1024 * 1024);
    auto secret = btrfsbackup::platform::linux::filesystem::create_sealed_secret_file(
        std::as_bytes(std::span(passphrase.data(), passphrase.size()))
    );

    btrfsbackup::platform::linux::storage::CryptsetupOperations operations;
    const std::string uuid = operations.format_luks2(image, secret.get());
    const auto header = operations.inspect_luks2(image);
    operations.test_key(image, secret.get());
    const auto metadata =
        btrfsbackup::platform::linux::storage::LibblkidBlockDeviceMetadataReader().read(image);

    test_helpers::expect_true("formatted LUKS UUID", !uuid.empty() && header.uuid == uuid, "LUKS UUID differs");
    test_helpers::expect_true("libblkid LUKS UUID", metadata.filesystem_uuid == uuid, "libblkid UUID differs");
    test_helpers::expect_true("initial LUKS keyslot", header.keyslots == std::vector<int>{0}, "initial keyslot missing");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_rejects_untrusted_inputs_before_libcryptsetup();
    test_formats_and_manages_a_luks2_header_without_a_process();
    return test_helpers::finish("cryptsetup operations tests");
}
