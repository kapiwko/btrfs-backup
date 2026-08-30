// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QString>

#include <vector>

namespace btrfsbackup::kde::monitor {

class NotificationDeduplicator final {
  public:
    explicit NotificationDeduplicator(QString state_path = {});

    [[nodiscard]] bool claim(
        const QString& profile_id,
        const QString& run_id,
        const QString& event_kind,
        const QDateTime& now = QDateTime::currentDateTimeUtc()
    );

  private:
    struct Entry {
        QString profile_id;
        QString run_id;
        QString event_kind;
        QDateTime handled_at;
    };

    void load();
    void prune(const QDateTime& now);
    void save() const;

    QString state_path_;
    std::vector<Entry> entries_;
};

} // namespace btrfsbackup::kde::monitor
