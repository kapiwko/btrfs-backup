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
    bool storage_enabled = true;
    int storage_warning_percent = 15;
    int storage_critical_percent = 5;
};

class BackupReminderSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int warningDays READ warningDays WRITE setWarningDays NOTIFY warningDaysChanged)
    Q_PROPERTY(int criticalDays READ criticalDays WRITE setCriticalDays NOTIFY criticalDaysChanged)
    Q_PROPERTY(bool storageEnabled READ storageEnabled WRITE setStorageEnabled NOTIFY storageEnabledChanged)
    Q_PROPERTY(int storageWarningPercent READ storageWarningPercent WRITE setStorageWarningPercent NOTIFY storageWarningPercentChanged)
    Q_PROPERTY(int storageCriticalPercent READ storageCriticalPercent WRITE setStorageCriticalPercent NOTIFY storageCriticalPercentChanged)

  public:
    explicit BackupReminderSettings(QObject* parent = nullptr);

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] int warningDays() const;
    [[nodiscard]] int criticalDays() const;
    [[nodiscard]] bool storageEnabled() const;
    [[nodiscard]] int storageWarningPercent() const;
    [[nodiscard]] int storageCriticalPercent() const;

    void setEnabled(bool enabled);
    void setWarningDays(int days);
    void setCriticalDays(int days);
    void setStorageEnabled(bool enabled);
    void setStorageWarningPercent(int percent);
    void setStorageCriticalPercent(int percent);

    [[nodiscard]] static BackupReminderConfiguration load();
    [[nodiscard]] static QString configFileName();

  signals:
    void enabledChanged();
    void warningDaysChanged();
    void criticalDaysChanged();
    void storageEnabledChanged();
    void storageWarningPercentChanged();
    void storageCriticalPercentChanged();

  private:
    void save() const;

    BackupReminderConfiguration configuration_;
};

} // namespace btrfsbackup::kde
