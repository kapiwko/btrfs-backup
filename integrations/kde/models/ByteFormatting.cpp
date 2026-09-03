// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ByteFormatting.hpp"

#include <array>

namespace btrfsbackup::kde {

QString format_byte_size(qint64 bytes, const QLocale& locale) {
    static constexpr std::array<const char*, 7> units{"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    const quint64 amount = bytes < 0 ? 0 : static_cast<quint64>(bytes);
    quint64 divisor = 1;
    std::size_t unit = 0;
    while (unit + 1 < units.size() && amount / divisor >= 1024) {
        divisor *= 1024;
        ++unit;
    }

    if (unit == 0) {
        return locale.toString(amount) + QStringLiteral(" B");
    }

    quint64 whole = amount / divisor;
    quint64 tenth = ((amount % divisor) * 10 + divisor / 2) / divisor;
    if (tenth == 10) {
        ++whole;
        tenth = 0;
    }
    return locale.toString(whole) + locale.decimalPoint() + locale.toString(tenth) +
        QStringLiteral(" ") + QString::fromLatin1(units.at(unit));
}

QString format_byte_rate(qint64 bytes_per_second, const QLocale& locale) {
    return format_byte_size(bytes_per_second, locale) + QStringLiteral("/s");
}

} // namespace btrfsbackup::kde
