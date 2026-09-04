// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/RealTrustedHookTest.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: btrfsbackup-real-trusted-hook-tests RUNTIME PROFILE TEST_ROOT\n";
        return 2;
    }
    try {
        btrfsbackup::integration::RealTrustedHookTest test(argv[1], argv[2], argv[3]);
        test.run();
        test.close();
        std::cout << "ok - runtime executes only pinned root-owned hooks from trusted directories\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "real-trusted-hook-tests: " << error.what() << '\n';
        return 1;
    }
}
