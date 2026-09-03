// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class BackupHistoryModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)

  public:
    explicit BackupHistoryModel(QObject* parent = nullptr);

    QVariantList entries() const;
    [[nodiscard]] bool apply(const QString& payload);
    void reset();

  signals:
    void changed();

  private:
    QVariantList entries_;
};
