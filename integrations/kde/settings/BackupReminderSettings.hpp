// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

namespace btrfsbackup::kde {

struct BackupReminderConfiguration {
    bool enabled = true;
    int warning_days = 7;
    int critical_days = 14;
};

class BackupReminderSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int warningDays READ warningDays WRITE setWarningDays NOTIFY warningDaysChanged)
    Q_PROPERTY(int criticalDays READ criticalDays WRITE setCriticalDays NOTIFY criticalDaysChanged)

  public:
    explicit BackupReminderSettings(QObject* parent = nullptr);

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] int warningDays() const;
    [[nodiscard]] int criticalDays() const;

    void setEnabled(bool enabled);
    void setWarningDays(int days);
    void setCriticalDays(int days);

    [[nodiscard]] static BackupReminderConfiguration load();
    [[nodiscard]] static QString configFileName();

  signals:
    void enabledChanged();
    void warningDaysChanged();
    void criticalDaysChanged();

  private:
    void save() const;

    BackupReminderConfiguration configuration_;
};

} // namespace btrfsbackup::kde
