// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ManagerApi.hpp"

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class ProfileDirectoryModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(bool managerConnected READ managerConnected NOTIFY managerConnectedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QString lastErrorCode READ lastErrorCode NOTIFY errorChanged)

  public:
    explicit ProfileDirectoryModel(QObject* parent = nullptr);

    QVariantList profiles() const;
    bool managerConnected() const;
    QString lastError() const;
    QString lastErrorCode() const;
    bool supports(const QString& feature) const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refreshNow();
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void openNotificationSettings();

  signals:
    void profilesChanged();
    void managerConnectedChanged();
    void errorChanged();
    void profileStatusInvalidated(const QString& profile_id);
    void profileHistoryInvalidated(const QString& profile_id);
    void profileDeviceStateInvalidated(const QString& profile_id);
    void refreshRequested();

  private:
    void connectToManager();
    void requestProfiles();
    void applyProfiles(const QString& payload);
    void setManagerConnected(bool connected);
    void setLastError(const QString& message, const QString& code = {});
    void managerUnavailable();

    QDBusConnection bus_;
    btrfsbackup::kde::ManagerEventSubscriber manager_events_;
    QDBusServiceWatcher service_watcher_;
    bool active_ = false;
    bool capabilities_verified_ = false;
    bool profiles_request_pending_ = false;
    bool profiles_refresh_queued_ = false;
    bool manager_connected_ = false;
    quint64 generation_ = 0;
    QSet<QString> features_;
    QVariantList profiles_;
    QString last_error_;
    QString last_error_code_;
};
