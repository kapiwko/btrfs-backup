// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ManagerTestClient.hpp"
#include "support/RealSystemDbusBackupTest.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--call") {
            btrfsbackup::integration::ManagerTestClient client;
            std::cout << client.call(argv[2], "default") << '\n';
            return 0;
        }
        if (argc != 4) {
            std::cerr << "usage: btrfsbackup-real-system-dbus-backup-tests "
                         "SOURCE_MOUNT TARGET_MOUNT TEST_ROOT\n";
            return 2;
        }
        btrfsbackup::integration::RealSystemDbusBackupTest test(
            argv[1], argv[2], argv[3], std::filesystem::canonical(argv[0])
        );
        test.run();
        std::cout << "ok - unprivileged user starts a real backup through system D-Bus and polkit\n"
                     "ok - system D-Bus backup transfers and verifies real Btrfs data\n";
        test.close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "real-system-dbus-backup-tests: " << error.what() << '\n';
        return 1;
    }
}
