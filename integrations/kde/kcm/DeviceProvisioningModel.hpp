// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace btrfsbackup::kde::kcm {

class DeviceProvisioningModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantMap operation READ operation NOTIFY operationChanged)
    Q_PROPERTY(QStringList sourceCandidates READ sourceCandidates NOTIFY sourceCandidatesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)

  public:
    explicit DeviceProvisioningModel(QObject* parent = nullptr);
    [[nodiscard]] QVariantList devices() const;
    [[nodiscard]] QVariantMap operation() const;
    [[nodiscard]] QStringList sourceCandidates() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString errorMessage() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void start(
        const QString& profile_id,
        const QString& profile_name,
        const QVariantMap& device,
        const QString& source_subvolume,
        const QString& passphrase,
        const QString& confirmation,
        bool automatic_key
    );
    Q_INVOKABLE void poll();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearError();

  signals:
    void devicesChanged();
    void operationChanged();
    void sourceCandidatesChanged();
    void stateChanged();
    void completed(const QString& profile_id);

  private:
    enum class RequestKind { Devices,
                             Sources,
                             Start,
                             Poll,
                             Cancel };
    void request(RequestKind kind, const QString& method, const QVariantList& arguments = {});
    bool applyDevices(const QString& payload);
    bool applySources(const QString& payload);
    bool applyOperation(const QString& payload);
    void setError(const QString& message);

    QDBusConnection bus_;
    QVariantList devices_;
    QVariantMap operation_;
    QStringList source_candidates_;
    QString error_message_;
    bool busy_ = false;
};

} // namespace btrfsbackup::kde::kcm
