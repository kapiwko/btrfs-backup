// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RunnerCommandParser.hpp"

#include <iostream>

int main() {
    namespace runner = btrfsbackup::kde::krunner;
    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    expect(runner::parse_command(QStringLiteral("Uruchom backup default"))->kind == runner::CommandKind::Start, "start not parsed");
    expect(runner::parse_command(QStringLiteral("Pokaż stan backupu"))->kind == runner::CommandKind::Status, "status not parsed");
    expect(runner::parse_command(QStringLiteral("Przeglądaj kopie default"))->kind == runner::CommandKind::Browse, "browse not parsed");
    expect(runner::parse_command(QStringLiteral("Znajdź poprzednie wersje /home/user/file"))->kind == runner::CommandKind::Versions, "versions not parsed");
    expect(runner::parse_command(QStringLiteral("Odłącz nośnik default"))->kind == runner::CommandKind::Eject, "eject not parsed");
    expect(!runner::parse_command(QStringLiteral("rm -rf /")), "unknown command accepted");
    expect(!runner::parse_command(QStringLiteral("Uruchom backup")), "missing argument accepted");
    return failures == 0 ? 0 : 1;
}
