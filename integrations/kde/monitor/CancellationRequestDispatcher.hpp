// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>

namespace btrfsbackup::kde::monitor {

class CancellationRequestDispatcher final : public QObject {
    Q_OBJECT

  public:
    explicit CancellationRequestDispatcher(QDBusConnection bus, QObject* parent = nullptr);

    void request(const QString& profile_id, const QString& run_id);

  signals:
    void accepted(const QString& profile_id, const QString& run_id);
    void rejected(const QString& profile_id, const QString& run_id, const QString& reason);

  private:
    QDBusConnection bus_;
};

} // namespace btrfsbackup::kde::monitor
