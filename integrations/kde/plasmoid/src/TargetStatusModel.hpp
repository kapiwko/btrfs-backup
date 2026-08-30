// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

class TargetStatusModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool unlocked READ unlocked NOTIFY changed)
    Q_PROPERTY(bool mounted READ mounted NOTIFY changed)
    Q_PROPERTY(bool safeToRemove READ safeToRemove NOTIFY changed)
    Q_PROPERTY(bool storageSupported READ storageSupported NOTIFY changed)
    Q_PROPERTY(bool storageKnown READ storageKnown NOTIFY changed)
    Q_PROPERTY(qint64 capacityBytes READ capacityBytes NOTIFY changed)
    Q_PROPERTY(qint64 usedBytes READ usedBytes NOTIFY changed)
    Q_PROPERTY(qint64 availableBytes READ availableBytes NOTIFY changed)
    Q_PROPERTY(int usagePercent READ usagePercent NOTIFY changed)
    Q_PROPERTY(bool storageLive READ storageLive NOTIFY changed)
    Q_PROPERTY(QString storageMeasuredAt READ storageMeasuredAt NOTIFY changed)
    Q_PROPERTY(bool spaceBelowMinimum READ spaceBelowMinimum NOTIFY changed)

  public:
    explicit TargetStatusModel(QObject* parent = nullptr);

    QString name() const;
    QString state() const;
    bool connected() const;
    bool unlocked() const;
    bool mounted() const;
    bool safeToRemove() const;
    bool storageSupported() const;
    bool storageKnown() const;
    qint64 capacityBytes() const;
    qint64 usedBytes() const;
    qint64 availableBytes() const;
    int usagePercent() const;
    bool storageLive() const;
    QString storageMeasuredAt() const;
    bool spaceBelowMinimum() const;

    void setStorageSupported(bool supported);
    [[nodiscard]] bool apply(const QString& profile_id, const QString& payload);
    void reset();

  signals:
    void changed();

  private:
    void resetStorage();

    QString name_;
    QString state_ = QStringLiteral("unknown");
    bool connected_ = false;
    bool unlocked_ = false;
    bool mounted_ = false;
    bool safe_to_remove_ = false;
    bool storage_supported_ = false;
    bool storage_known_ = false;
    qint64 capacity_bytes_ = 0;
    qint64 used_bytes_ = 0;
    qint64 available_bytes_ = 0;
    int usage_percent_ = -1;
    bool storage_live_ = false;
    QString storage_measured_at_;
    bool space_below_minimum_ = false;
};
