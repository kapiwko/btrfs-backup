// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BackupHistoryModel.hpp"
#include "ManagerApi.hpp"
#include "RunStatusModel.hpp"
#include "TargetStatusModel.hpp"

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class BackupStatusModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY profileChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString profileName READ profileName NOTIFY statusChanged)
    Q_PROPERTY(bool managerConnected READ managerConnected NOTIFY managerConnectedChanged)
    Q_PROPERTY(RunStatusModel* run READ run CONSTANT)
    Q_PROPERTY(TargetStatusModel* target READ target CONSTANT)
    Q_PROPERTY(BackupHistoryModel* history READ history CONSTANT)
    Q_PROPERTY(bool operationPending READ operationPending NOTIFY operationChanged)
    Q_PROPERTY(QString lastOperation READ lastOperation NOTIFY operationChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(bool browseSupported READ browseSupported NOTIFY managerConnectedChanged)

  public:
    explicit BackupStatusModel(QObject* parent = nullptr);

    QString profile() const;
    void setProfile(const QString& profile);

    bool managerConnected() const;
    QVariantList profiles() const;
    QString profileName() const;
    RunStatusModel* run();
    TargetStatusModel* target();
    BackupHistoryModel* history();
    bool operationPending() const;
    QString lastOperation() const;
    QString lastError() const;
    bool browseSupported() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refreshNow();
    Q_INVOKABLE void startBackup();
    Q_INVOKABLE void cancelBackup();
    Q_INVOKABLE void validateTarget();
    Q_INVOKABLE void ejectTarget();
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void browseBackups();

  signals:
    void profileChanged();
    void managerConnectedChanged();
    void profilesChanged();
    void statusChanged();
    void targetChanged();
    void historyChanged();
    void operationChanged();
    void errorChanged();

  private:
    void connectToManager();
    void requestProfiles();
    void requestStatus();
    void requestDeviceState();
    void requestHistory();
    void applyProfiles(const QString& payload);
    void applyStatus(const QString& payload);
    void applyDeviceState(const QString& payload);
    void applyHistory(const QString& payload);
    void requestOperation(const QString& method, const QVariantList& arguments);
    bool supports(const QString& feature) const;
    void setManagerConnected(bool connected);
    void setLastError(const QString& message);
    void managerUnavailable();

    QString profile_ = QStringLiteral("default");
    QDBusConnection bus_;
    btrfsbackup::kde::ManagerEventSubscriber manager_events_;
    QDBusServiceWatcher service_watcher_;
    QTimer operation_message_timer_;
    RunStatusModel run_;
    TargetStatusModel target_;
    BackupHistoryModel history_;
    bool active_ = false;
    bool capabilities_verified_ = false;
    bool profiles_request_pending_ = false;
    bool status_request_pending_ = false;
    bool device_request_pending_ = false;
    bool history_request_pending_ = false;
    bool profiles_refresh_queued_ = false;
    bool status_refresh_queued_ = false;
    bool device_refresh_queued_ = false;
    bool history_refresh_queued_ = false;
    bool manager_connected_ = false;
    bool operation_pending_ = false;
    quint64 generation_ = 0;
    QSet<QString> features_;
    QVariantList profiles_;
    QString profile_name_;
    QString last_operation_;
    QString last_error_;
};
