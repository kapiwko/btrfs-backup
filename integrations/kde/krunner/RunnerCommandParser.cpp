// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RunnerCommandParser.hpp"

namespace btrfsbackup::kde::krunner {

std::optional<ParsedCommand> parse_command(const QString& raw_query) {
    const QString query = raw_query.simplified();
    if (query.compare(QStringLiteral("Pokaż stan backupu"), Qt::CaseInsensitive) == 0 ||
        query.compare(QStringLiteral("Pokaz stan backupu"), Qt::CaseInsensitive) == 0)
        return ParsedCommand{CommandKind::Status, {}};
    const struct Prefix { const char* text; CommandKind kind; } prefixes[] = {
        {"Uruchom backup ", CommandKind::Start},
        {"Przeglądaj kopie ", CommandKind::Browse},
        {"Przegladaj kopie ", CommandKind::Browse},
        {"Znajdź poprzednie wersje ", CommandKind::Versions},
        {"Znajdz poprzednie wersje ", CommandKind::Versions},
        {"Odłącz nośnik ", CommandKind::Eject},
        {"Odlacz nosnik ", CommandKind::Eject},
    };
    for (const Prefix& prefix : prefixes) {
        const QString text = QString::fromUtf8(prefix.text);
        if (query.startsWith(text, Qt::CaseInsensitive)) {
            const QString argument = query.mid(text.size()).trimmed();
            if (!argument.isEmpty())
                return ParsedCommand{prefix.kind, argument};
        }
    }
    return std::nullopt;
}

} // namespace btrfsbackup::kde::krunner
