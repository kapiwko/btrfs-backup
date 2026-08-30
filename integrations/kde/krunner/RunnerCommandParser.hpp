// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <optional>

namespace btrfsbackup::kde::krunner {

enum class CommandKind { Start, Status, Browse, Versions, Eject };
struct ParsedCommand { CommandKind kind; QString argument; };

[[nodiscard]] std::optional<ParsedCommand> parse_command(const QString& query);

} // namespace btrfsbackup::kde::krunner
