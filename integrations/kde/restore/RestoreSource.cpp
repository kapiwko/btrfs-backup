// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RestoreSource.hpp"

using Qt::StringLiterals::operator""_s;

namespace btrfsbackup::kde::restore {

std::optional<RestoreSource> parse_restore_source(const QUrl& url) {
    if (url.scheme() != u"btrfsbackup"_s || !url.host().isEmpty() || url.hasQuery() || url.hasFragment())
        return std::nullopt;
    const QString decoded = url.path(QUrl::FullyDecoded);
    if (decoded.contains(QChar::Null) || decoded.contains(u"//"_s))
        return std::nullopt;
    const QStringList parts = decoded.split(u'/', Qt::SkipEmptyParts);
    if (parts.size() < 2 || parts.at(1) == u".versions"_s)
        return std::nullopt;
    for (const QString& part : parts)
        if (part == u"."_s || part == u".."_s)
            return std::nullopt;
    return RestoreSource{
        .profile_id = parts.at(0),
        .snapshot_id = parts.at(1),
        .relative_path = parts.size() > 2 ? parts.mid(2).join(u'/') : u"."_s,
    };
}

} // namespace btrfsbackup::kde::restore
