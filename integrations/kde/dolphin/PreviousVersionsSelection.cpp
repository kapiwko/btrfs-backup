// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PreviousVersionsSelection.hpp"

namespace btrfsbackup::kde::dolphin {

bool can_offer_previous_versions(const QList<QUrl>& urls, bool selected_item_is_symlink) {
    return urls.size() == 1 && urls.front().isLocalFile() && !urls.front().toLocalFile().isEmpty() &&
        !selected_item_is_symlink;
}

} // namespace btrfsbackup::kde::dolphin
