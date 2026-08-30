// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PreviousVersionsSelection.hpp"

#include <iostream>

int main() {
    using btrfsbackup::kde::dolphin::can_offer_previous_versions;
    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    expect(can_offer_previous_versions({QUrl::fromLocalFile(QStringLiteral("/home/user/file"))}, false), "local file rejected");
    expect(!can_offer_previous_versions({}, false), "empty selection accepted");
    expect(!can_offer_previous_versions({QUrl::fromLocalFile(QStringLiteral("/a")), QUrl::fromLocalFile(QStringLiteral("/b"))}, false), "multiple selection accepted");
    expect(!can_offer_previous_versions({QUrl(QStringLiteral("sftp://host/file"))}, false), "remote URL accepted");
    expect(!can_offer_previous_versions({QUrl::fromLocalFile(QStringLiteral("/home/user/link"))}, true), "symlink accepted");
    return failures == 0 ? 0 : 1;
}
