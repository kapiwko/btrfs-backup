// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QJsonObject>
#include <QObject>
#include <QVariantList>

#include <ManagerApi.hpp>

namespace btrfsbackup::kde::kcm {

class ProfileConfigurationModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString profileId READ profileId NOTIFY profileChanged)
    Q_PROPERTY(QString name READ name NOTIFY profileChanged)
    Q_PROPERTY(bool enabled READ enabled NOTIFY profileChanged)
    Q_PROPERTY(QVariantMap target READ target NOTIFY profileChanged)
    Q_PROPERTY(QVariantMap paths READ paths NOTIFY profileChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY profileChanged)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY profileChanged)
    Q_PROPERTY(QVariantList sourceCandidates READ sourceCandidates NOTIFY profileChanged)
    Q_PROPERTY(bool configurationValid READ configurationValid NOTIFY profileChanged)
    Q_PROPERTY(QString configurationErrorCode READ configurationErrorCode NOTIFY profileChanged)
    Q_PROPERTY(int schemaVersion READ schemaVersion NOTIFY profileChanged)
    Q_PROPERTY(QString generation READ generation NOTIFY profileChanged)
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY profileChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString operationMessage READ operationMessage NOTIFY stateChanged)

  public:
    explicit ProfileConfigurationModel(QObject* parent = nullptr);

    QString profileId() const;
    QString name() const;
    bool enabled() const;
    QVariantMap target() const;
    QVariantMap paths() const;
    QVariantMap settings() const;
    QVariantList sources() const;
    QVariantList sourceCandidates() const;
    bool configurationValid() const;
    QString configurationErrorCode() const;
    int schemaVersion() const;
    QString generation() const;
    QString fingerprint() const;
    bool loaded() const;
    bool busy() const;
    QString errorCode() const;
    QString errorMessage() const;
    QString operationMessage() const;

    Q_INVOKABLE void load(const QString& profileId);
    Q_INVOKABLE void loadDetails(const QString& profileId);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void addSourceConfiguration(const QString& name, const QString& subvolume, int localRetention, int remoteRetention);
    Q_INVOKABLE void updateSourceConfiguration(int index, const QString& name, int localRetention, int remoteRetention);
    Q_INVOKABLE void removeSourceConfiguration(int index);
    Q_INVOKABLE void updateProfileSettings(const QString& name, bool dailyLimit, bool autoEject);
    Q_INVOKABLE void deleteProfile();

  signals:
    void profileChanged();
    void stateChanged();
    void conflictDetected();
    void profileSaved(const QString& profileId);
    void profileDeleted(const QString& profileId);

  private:
    enum class RequestKind { LoadDetails,
                             UpdateSettings,
                             AddSource,
                             UpdateSource,
                             RemoveSource,
                             Delete };
    void request(RequestKind kind, const QString& method, const QVariantList& arguments);
    bool applyEnvelope(const QString& payload);
    void setError(const QString& code, const QString& message);
    void setBusy(bool value);
    QJsonObject object(const char* key) const;

    QDBusConnection bus_;
    btrfsbackup::kde::ManagerEventSubscriber manager_events_;
    QJsonObject profile_;
    QString generation_;
    QString fingerprint_;
    QVariantList source_candidates_;
    bool configuration_valid_ = true;
    QString configuration_error_code_;
    QString error_code_;
    QString error_message_;
    QString operation_message_;
    bool loaded_ = false;
    bool busy_ = false;
    bool refresh_pending_ = false;
};

} // namespace btrfsbackup::kde::kcm
