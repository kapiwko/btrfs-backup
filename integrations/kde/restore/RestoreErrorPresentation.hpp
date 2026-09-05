// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <restore/RestoreError.hpp>

namespace btrfsbackup::kde::restore {

struct RestoreErrorPresentation {
    QString message;
    QString code;
    QString technical_details;
};

[[nodiscard]] RestoreErrorPresentation present_restore_error(
    btrfsbackup::restore::RestoreErrorCode code,
    const QString& technical_details
);
[[nodiscard]] RestoreErrorPresentation present_unexpected_restore_error(const QString& technical_details);

} // namespace btrfsbackup::kde::restore
