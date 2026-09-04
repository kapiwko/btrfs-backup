// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/RealManagerIndependenceTest.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "usage: btrfsbackup-real-manager-independence-tests "
                     "RUNTIME PROFILE TEST_ROOT SOURCE_MOUNT TARGET_MOUNT\n";
        return 2;
    }
    try {
        btrfsbackup::integration::RealManagerIndependenceTest test(
            argv[1], argv[2], argv[3], argv[4], argv[5]
        );
        test.run();
        std::cout << "ok - active runner completes after the system manager stops\n";
        test.close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "real-manager-independence-tests: " << error.what() << '\n';
        return 1;
    }
}
