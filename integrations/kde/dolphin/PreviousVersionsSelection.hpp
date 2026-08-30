// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QUrl>

namespace btrfsbackup::kde::dolphin {

[[nodiscard]] bool can_offer_previous_versions(const QList<QUrl>& urls, bool selected_item_is_symlink);

} // namespace btrfsbackup::kde::dolphin
