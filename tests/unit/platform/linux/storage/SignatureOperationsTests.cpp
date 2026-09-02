// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/SignatureOperations.hpp>

#include <core/Errors.hpp>

#include "support/TestHelpers.hpp"

namespace {

void test_rejects_relative_path() {
    btrfsbackup::platform::linux::storage::LibblkidSignatureOperations operations;
    try {
        operations.wipe_all("dev/test", "8:16");
        test_helpers::fail("relative signature target", "relative path was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
}

void test_rejects_regular_file() {
    const auto root = test_helpers::test_root("signature-operations", "regular-file");
    const auto path = root / "target";
    test_helpers::write_file(path, "data");
    btrfsbackup::platform::linux::storage::LibblkidSignatureOperations operations;
    try {
        operations.wipe_all(path, "8:16");
        test_helpers::fail("regular signature target", "regular file was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
}

} // namespace

int main() {
    test_rejects_relative_path();
    test_rejects_regular_file();
    return test_helpers::finish("signature operations tests");
}
