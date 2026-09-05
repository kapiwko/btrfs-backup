// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QUrl>

#include <cstdint>
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

struct PreviousVersion {
    QString snapshot_id;
    QDateTime created_at;
    bool directory = false;
    std::uint64_t size = 0;
    std::uint32_t mode = 0;
    std::int64_t modified_at = 0;
};

struct PreviousVersionsPage {
    std::vector<PreviousVersion> entries;
    QString continuation_token;
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
[[nodiscard]] std::optional<PreviousVersionsPage> parse_previous_versions_page(const QString& payload);
[[nodiscard]] bool previous_versions_method_unavailable(const QString& dbus_error_name);

} // namespace btrfsbackup::kde::kio
