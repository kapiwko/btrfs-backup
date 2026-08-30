// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ManagerApi.hpp"
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
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    Q_PROPERTY(QString runId READ runId NOTIFY statusChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY statusChanged)
    Q_PROPERTY(QString activity READ activity NOTIFY statusChanged)
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY statusChanged)
    Q_PROPERTY(QString currentSourceName READ currentSourceName NOTIFY statusChanged)
    Q_PROPERTY(QString targetName READ targetName NOTIFY statusChanged)
    Q_PROPERTY(qint64 speedBps READ speedBps NOTIFY statusChanged)
    Q_PROPERTY(int etaSeconds READ etaSeconds NOTIFY statusChanged)
    Q_PROPERTY(int sourceProgress READ sourceProgress NOTIFY statusChanged)
    Q_PROPERTY(int overallProgress READ overallProgress NOTIFY statusChanged)
    Q_PROPERTY(QString progressAccuracy READ progressAccuracy NOTIFY statusChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY statusChanged)
    Q_PROPERTY(TargetStatusModel* target READ target CONSTANT)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(bool operationPending READ operationPending NOTIFY operationChanged)
    Q_PROPERTY(QString lastOperation READ lastOperation NOTIFY operationChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)

  public:
    explicit BackupStatusModel(QObject* parent = nullptr);

    QString profile() const;
    void setProfile(const QString& profile);

    bool managerConnected() const;
    QVariantList profiles() const;
    QString profileName() const;
    QString state() const;
    QString runId() const;
    QString phase() const;
    QString activity() const;
    bool canCancel() const;
    QString currentSourceName() const;
    QString targetName() const;
    qint64 speedBps() const;
    int etaSeconds() const;
    int sourceProgress() const;
    int overallProgress() const;
    QString progressAccuracy() const;
    QString errorCode() const;
    TargetStatusModel* target();
    QVariantList history() const;
    bool operationPending() const;
    QString lastOperation() const;
    QString lastError() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refreshNow();
    Q_INVOKABLE void startBackup();
    Q_INVOKABLE void cancelBackup();
    Q_INVOKABLE void validateTarget();
    Q_INVOKABLE void ejectTarget();

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
    TargetStatusModel target_;
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
    QString run_id_;
    QString state_ = QStringLiteral("unknown");
    QString phase_ = QStringLiteral("idle");
    QString activity_ = QStringLiteral("idle");
    bool can_cancel_ = false;
    QString current_source_name_;
    QString target_name_;
    qint64 speed_bps_ = 0;
    int eta_seconds_ = -1;
    int source_progress_ = -1;
    int overall_progress_ = -1;
    QString progress_accuracy_ = QStringLiteral("indeterminate");
    QString error_code_;
    QVariantList history_;
    QString last_operation_;
    QString last_error_;
};
