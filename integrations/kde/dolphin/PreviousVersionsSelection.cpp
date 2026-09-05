// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PreviousVersionsSelection.hpp"

#include "ManagerApi.hpp"

using Qt::StringLiterals::operator""_s;

namespace btrfsbackup::kde::dolphin {

bool can_offer_previous_versions(const QList<QUrl>& urls, bool selected_item_is_symlink) {
    return urls.size() == 1 && urls.front().isLocalFile() && !urls.front().toLocalFile().isEmpty() &&
        !selected_item_is_symlink;
}

PreviousVersionsOutcome classify_previous_versions(
    bool request_succeeded,
    bool document_valid,
    bool has_match
) {
    if (!request_succeeded || !document_valid)
        return PreviousVersionsOutcome::ServiceError;
    return has_match ? PreviousVersionsOutcome::Open : PreviousVersionsOutcome::NotFound;
}

std::optional<QUrl> select_previous_versions_url(
    const QList<btrfsbackup::kde::BackupCoverage>& coverage,
    qsizetype selected_index
) {
    if (selected_index < 0 || selected_index >= coverage.size())
        return std::nullopt;
    const auto& selected = coverage.at(selected_index);
    if (selected.profile_id.isEmpty() || selected.source_id.isEmpty() || selected.relative_path.isEmpty())
        return std::nullopt;
    QUrl url;
    url.setScheme(u"btrfsbackup"_s);
    QString path = u"/"_s + selected.profile_id + u"/.versions/"_s + selected.source_id;
    if (selected.relative_path != u"."_s)
        path += u"/"_s + selected.relative_path;
    url.setPath(path);
    return url;
}

} // namespace btrfsbackup::kde::dolphin
