// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <filesystem>

#include <platform/linux/SystemdUnit.hpp>

#include "support/TestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

void test_unit_file_follows_load_path_precedence() {
    const fs::path root = test_helpers::test_root("systemd-unit", "load-path");
    const fs::path admin_root = root / "etc";
    const fs::path package_root = root / "usr";
    const std::string unit_name = "btrfs-backup-target@.service";
    test_helpers::write_file(package_root / unit_name, "package\n");

    const std::array<fs::path, 2> roots = {admin_root, package_root};
    auto located = btrfsbackup::platform::linux::locate_systemd_unit_file(unit_name, roots);
    test_helpers::expect_true(
        "package unit fallback",
        located == package_root / unit_name,
        "package unit was not found"
    );

    test_helpers::write_file(admin_root / unit_name, "override\n");
    located = btrfsbackup::platform::linux::locate_systemd_unit_file(unit_name, roots);
    test_helpers::expect_true(
        "admin unit precedence",
        located == admin_root / unit_name,
        "admin override did not take precedence"
    );

    test_helpers::expect_true(
        "unsafe unit name",
        !btrfsbackup::platform::linux::locate_systemd_unit_file("../outside.service", roots).has_value(),
        "unit lookup accepted a path traversal"
    );
    fs::remove_all(root);
}

} // namespace

int main() {
    test_unit_file_follows_load_path_precedence();
    return test_helpers::finish("systemd unit tests");
}
