// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLocale>
#include <QString>
#include <QtTypes>

namespace btrfsbackup::kde {

[[nodiscard]] QString format_byte_size(qint64 bytes, const QLocale& locale = QLocale());
[[nodiscard]] QString format_byte_rate(qint64 bytes_per_second, const QLocale& locale = QLocale());

} // namespace btrfsbackup::kde
