#include <filesystem>
#include <limits>
#include <string>

#include <platform/linux/space_check.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

void test_available_bytes() {
    fs::path root = test_helpers::test_root("space-check", "available");
    unsigned long long available = btrfsbackup::available_bytes(root);
    test_helpers::expect_true("space available", available > 0, "temporary directory should report available bytes");
    fs::remove_all(root);
}

void test_minimum_free_space() {
    fs::path root = test_helpers::test_root("space-check", "minimum");
    btrfsbackup::check_minimum_free_space(root, 0, "test");
    btrfsbackup::check_minimum_free_space(root, 1, "test");
    test_helpers::expect_validation_error("space too low", [&] {
        btrfsbackup::check_minimum_free_space(root, std::numeric_limits<unsigned long long>::max(), "test");
    }, "Insufficient free space");
    fs::remove_all(root);
}

void test_missing_path() {
    test_helpers::expect_validation_error("space missing", [] {
        (void)btrfsbackup::available_bytes("/tmp/does-not-exist-btrfs-backup-space-check");
    }, "Could not determine free space");
}

} // namespace

int main() {
    test_available_bytes();
    test_minimum_free_space();
    test_missing_path();

    return test_helpers::finish("space check tests");
}
