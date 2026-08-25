// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

class BackupStatusModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY profileChanged)
    Q_PROPERTY(QString profileName READ profileName NOTIFY statusChanged)
    Q_PROPERTY(bool managerConnected READ managerConnected NOTIFY managerConnectedChanged)
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    Q_PROPERTY(QString currentSourceName READ currentSourceName NOTIFY statusChanged)
    Q_PROPERTY(QString targetName READ targetName NOTIFY statusChanged)
    Q_PROPERTY(qint64 speedBps READ speedBps NOTIFY statusChanged)
    Q_PROPERTY(int etaSeconds READ etaSeconds NOTIFY statusChanged)
    Q_PROPERTY(int sourceProgress READ sourceProgress NOTIFY statusChanged)
    Q_PROPERTY(int overallProgress READ overallProgress NOTIFY statusChanged)
    Q_PROPERTY(QString progressAccuracy READ progressAccuracy NOTIFY statusChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)

  public:
    explicit BackupStatusModel(QObject* parent = nullptr);

    QString profile() const;
    void setProfile(const QString& profile);

    bool managerConnected() const;
    QString profileName() const;
    QString state() const;
    QString currentSourceName() const;
    QString targetName() const;
    qint64 speedBps() const;
    int etaSeconds() const;
    int sourceProgress() const;
    int overallProgress() const;
    QString progressAccuracy() const;
    QString errorCode() const;
    QString lastError() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

  signals:
    void profileChanged();
    void managerConnectedChanged();
    void statusChanged();
    void errorChanged();

  private:
    void connectToManager();
    void refresh();
    void requestProfiles();
    void requestStatus();
    void applyProfiles(const QString& payload);
    void applyStatus(const QString& payload);
    void setManagerConnected(bool connected);
    void setLastError(const QString& message);
    void managerUnavailable();

    QString profile_ = QStringLiteral("default");
    QDBusConnection bus_;
    QDBusServiceWatcher service_watcher_;
    QTimer poll_timer_;
    bool active_ = false;
    bool capabilities_verified_ = false;
    bool status_request_pending_ = false;
    bool manager_connected_ = false;
    quint64 generation_ = 0;
    QString profile_name_;
    QString state_ = QStringLiteral("unknown");
    QString current_source_name_;
    QString target_name_;
    qint64 speed_bps_ = 0;
    int eta_seconds_ = -1;
    int source_progress_ = -1;
    int overall_progress_ = -1;
    QString progress_accuracy_ = QStringLiteral("indeterminate");
    QString error_code_;
    QString last_error_;
};
