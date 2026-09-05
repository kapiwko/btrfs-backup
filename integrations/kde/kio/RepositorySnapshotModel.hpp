// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QUrl>

#include <optional>
#include <vector>

namespace btrfsbackup::kde::kio {

struct RepositorySnapshot {
    QString id;
    QString profile_id;
    QString source_id;
    QString repository_path;
    QDateTime created_at;
    bool verified = false;
};

[[nodiscard]] std::optional<QHash<QString, RepositorySnapshot>> parse_repository_snapshots(
    const QString& payload
);
[[nodiscard]] std::vector<RepositorySnapshot> matching_versions(
    const QHash<QString, RepositorySnapshot>& snapshots,
    const QString& profile_id,
    const QString& source_id
);
[[nodiscard]] std::optional<QUrl> version_target_url(
    const QString& profile_id,
    const QString& snapshot_id,
    const QString& requested_path
);

} // namespace btrfsbackup::kde::kio
