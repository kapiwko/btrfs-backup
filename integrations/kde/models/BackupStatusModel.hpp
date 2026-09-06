// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BackupHistoryModel.hpp"
#include "ProfileDirectoryModel.hpp"
#include "RunStatusModel.hpp"
#include "TargetStatusModel.hpp"

#include <QDBusConnection>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class BackupStatusModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY profileChanged)
    Q_PROPERTY(QString profileName READ profileName NOTIFY statusChanged)
    Q_PROPERTY(bool profileEnabled READ profileEnabled NOTIFY statusChanged)
    Q_PROPERTY(bool configurationValid READ configurationValid NOTIFY statusChanged)
    Q_PROPERTY(QString configurationErrorCode READ configurationErrorCode NOTIFY statusChanged)
    Q_PROPERTY(bool managerConnected READ managerConnected NOTIFY managerConnectedChanged)
    Q_PROPERTY(RunStatusModel* run READ run CONSTANT)
    Q_PROPERTY(TargetStatusModel* target READ target CONSTANT)
    Q_PROPERTY(BackupHistoryModel* history READ history CONSTANT)
    Q_PROPERTY(bool operationPending READ operationPending NOTIFY operationChanged)
    Q_PROPERTY(QString lastOperation READ lastOperation NOTIFY operationChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QString lastErrorCode READ lastErrorCode NOTIFY errorChanged)
    Q_PROPERTY(bool browseSupported READ browseSupported NOTIFY managerConnectedChanged)
    Q_PROPERTY(int historyLimit READ historyLimit WRITE setHistoryLimit NOTIFY historyLimitChanged)
    Q_PROPERTY(ProfileDirectoryModel* directory READ directory WRITE setDirectory NOTIFY directoryChanged)

  public:
    explicit BackupStatusModel(QObject* parent = nullptr);

    QString profile() const;
    void setProfile(const QString& profile);
    bool managerConnected() const;
    QString profileName() const;
    bool profileEnabled() const;
    bool configurationValid() const;
    QString configurationErrorCode() const;
    RunStatusModel* run();
    TargetStatusModel* target();
    BackupHistoryModel* history();
    bool operationPending() const;
    QString lastOperation() const;
    QString lastError() const;
    QString lastErrorCode() const;
    bool browseSupported() const;
    int historyLimit() const;
    void setHistoryLimit(int limit);
    ProfileDirectoryModel* directory() const;
    void setDirectory(ProfileDirectoryModel* directory);

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refreshNow();
    Q_INVOKABLE void startBackup();
    Q_INVOKABLE void cancelBackup();
    Q_INVOKABLE void validateTarget();
    Q_INVOKABLE void ejectTarget();
    Q_INVOKABLE void setProfileEnabled(bool enabled);
    Q_INVOKABLE void browseBackups();

  signals:
    void profileChanged();
    void managerConnectedChanged();
    void statusChanged();
    void targetChanged();
    void historyChanged();
    void operationChanged();
    void errorChanged();
    void historyLimitChanged();
    void directoryChanged();

  private:
    void requestStatus();
    void requestDeviceState();
    void requestHistory();
    void syncFromDirectory();
    void applyStatus(const QString& payload);
    void applyDeviceState(const QString& payload);
    void requestOperation(const QString& method, const QVariantList& arguments);
    void setManagerConnected(bool connected);
    void setLastError(const QString& message, const QString& code = {});
    void managerUnavailable();
    void connectDirectory();

    QString profile_ = QStringLiteral("default");
    QDBusConnection bus_;
    ProfileDirectoryModel local_directory_;
    QPointer<ProfileDirectoryModel> directory_;
    QTimer operation_message_timer_;
    RunStatusModel run_;
    TargetStatusModel target_;
    BackupHistoryModel history_;
    bool active_ = false;
    bool status_request_pending_ = false;
    bool device_request_pending_ = false;
    bool status_refresh_queued_ = false;
    bool device_refresh_queued_ = false;
    bool manager_connected_ = false;
    bool operation_pending_ = false;
    quint64 generation_ = 0;
    QString profile_name_;
    bool profile_enabled_ = true;
    bool configuration_valid_ = true;
    QString configuration_error_code_;
    QString last_operation_;
    QString last_error_;
    QString last_error_code_;
    int history_limit_ = 3;
};
