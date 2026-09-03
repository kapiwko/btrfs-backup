// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ManagerApi.hpp"

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QVariantList>

class BackupHistoryModel final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString profileId READ profileId WRITE setProfileId NOTIFY profileIdChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY stateChanged)
    Q_PROPERTY(int pageSize READ pageSize WRITE setPageSize NOTIFY pageSizeChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY stateChanged)

  public:
    explicit BackupHistoryModel(QObject* parent = nullptr);

    QString profileId() const;
    void setProfileId(const QString& profile_id);
    QVariantList entries() const;
    bool loading() const;
    bool hasMore() const;
    int pageSize() const;
    void setPageSize(int page_size);
    QString errorCode() const;

    [[nodiscard]] bool apply(const QString& payload);
    void reset();
    Q_INVOKABLE void loadFirstPage();
    Q_INVOKABLE void loadMore();

  signals:
    void profileIdChanged();
    void changed();
    void pageSizeChanged();
    void stateChanged();

  private:
    void ensureCapabilities(bool replace);
    void requestPage(bool replace);
    void setError(const QString& code);
    [[nodiscard]] static bool parseEntries(const QString& payload, QVariantList& entries);

    QDBusConnection bus_;
    btrfsbackup::kde::ManagerEventSubscriber manager_events_;
    QString profile_id_;
    QVariantList entries_;
    QString error_code_;
    bool loading_ = false;
    bool has_more_ = false;
    bool capabilities_verified_ = false;
    bool reload_queued_ = false;
    int page_size_ = 10;
    quint64 generation_ = 0;
};
