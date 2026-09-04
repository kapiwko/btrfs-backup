// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/RealSandboxedSystemdTest.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "usage: btrfsbackup-real-sandboxed-systemd-tests "
                     "SOURCE_MOUNT TARGET_MOUNT STAGING_MOUNT MAPPER_NAME PROFILE\n";
        return 2;
    }
    try {
        btrfsbackup::integration::RealSandboxedSystemdTest test(argv[1], argv[2], argv[3], argv[4], argv[5]);
        test.run_sandboxed_backup();
        std::cout << "ok - sandboxed systemd service completes a real Btrfs backup\n";
        test.run_automatic_eject();
        std::cout << "ok - automatic eject runs outside the backup mount namespace\n";
        test.close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "real-sandboxed-systemd-tests: " << error.what() << '\n';
        return 1;
    }
}
