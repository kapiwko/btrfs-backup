// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QUrl>

#include <optional>

namespace btrfsbackup::kde::restore {

struct RestoreSource {
    QString profile_id;
    QString snapshot_id;
    QString relative_path;
};

[[nodiscard]] std::optional<RestoreSource> parse_restore_source(const QUrl& url);

} // namespace btrfsbackup::kde::restore
