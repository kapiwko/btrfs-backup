// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QUrl>

#include <optional>

namespace btrfsbackup::kde {
struct BackupCoverage;
}

namespace btrfsbackup::kde::dolphin {

enum class PreviousVersionsOutcome {
    Open,
    NotFound,
    ServiceError,
};

[[nodiscard]] bool can_offer_previous_versions(const QList<QUrl>& urls, bool selected_item_is_symlink);
[[nodiscard]] PreviousVersionsOutcome classify_previous_versions(
    bool request_succeeded,
    bool document_valid,
    bool has_match
);
[[nodiscard]] std::optional<QUrl> select_previous_versions_url(
    const QList<btrfsbackup::kde::BackupCoverage>& coverage,
    qsizetype selected_index
);

} // namespace btrfsbackup::kde::dolphin
