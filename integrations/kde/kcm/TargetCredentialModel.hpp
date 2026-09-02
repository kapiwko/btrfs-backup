// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QVariantList>
#include <QUrl>

#include <ManagerApi.hpp>

namespace btrfsbackup::kde::kcm {

class TargetCredentialModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString profileId READ profileId NOTIFY stateChanged)
    Q_PROPERTY(QVariantList credentials READ credentials NOTIFY credentialsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)

  public:
    explicit TargetCredentialModel(QObject* parent = nullptr);

    [[nodiscard]] QString profileId() const;
    [[nodiscard]] QVariantList credentials() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString errorCode() const;
    [[nodiscard]] QString errorMessage() const;

    Q_INVOKABLE void load(const QString& profile_id);
    Q_INVOKABLE void addPassphrase(
        const QString& authorization_secret,
        const QString& new_secret,
        const QString& confirmation,
        const QString& label
    );
    Q_INVOKABLE void generateKey(
        const QString& authorization_secret,
        const QString& label,
        bool automatic
    );
    Q_INVOKABLE void addKey(
        const QString& authorization_secret,
        const QUrl& key_file,
        const QString& label,
        bool automatic
    );
    Q_INVOKABLE void removeCredential(const QString& credential_id, const QString& authorization_secret);
    Q_INVOKABLE void clearError();

  signals:
    void credentialsChanged();
    void stateChanged();

  private:
    enum class RequestKind { List,
                             Mutation };
    void request(RequestKind kind, const QString& method, const QVariantList& arguments);
    void setError(const QString& code, const QString& message);
    [[nodiscard]] bool applyCredentials(const QString& payload);

    QDBusConnection bus_;
    btrfsbackup::kde::ManagerEventSubscriber manager_events_;
    QString profile_id_;
    QVariantList credentials_;
    QString error_code_;
    QString error_message_;
    bool busy_ = false;
    bool refresh_pending_ = false;
};

} // namespace btrfsbackup::kde::kcm
