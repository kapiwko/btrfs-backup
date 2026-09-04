// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/RealInstalledRuntimeTest.hpp"

#include <exception>
#include <iostream>

using btrfsbackup::integration::RealInstalledRuntimeTest;

int main(int argc, char** argv) {
    if (argc != 9) {
        std::cerr << "usage: btrfsbackup-real-installed-runtime-tests BACKUPCTL RUNTIME TEST_ROOT "
                     "SOURCE_MOUNT TARGET_MOUNT TARGET_DEVICE MAPPER_NAME PASSPHRASE_FILE\n";
        return 2;
    }
    try {
        RealInstalledRuntimeTest test(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8]);
        test.configure_and_install();
        std::cout << "ok - installed CLI renders, installs, and validates configuration\n";
        test.activate_managed_target();
        std::cout << "ok - native mount unit activates LUKS without fstab or crypttab\n";
        test.validate_runtime();
        std::cout << "ok - installed runtime and mount command validate the mounted target\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "real-installed-runtime-tests: " << error.what() << '\n';
        return 1;
    }
}
