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
    Q_PROPERTY(QVariantMap topology READ topology NOTIFY topologyChanged)
    Q_PROPERTY(QVariantMap plan READ plan NOTIFY planChanged)
    Q_PROPERTY(QVariantMap inspection READ inspection NOTIFY inspectionChanged)
    Q_PROPERTY(QVariantMap operation READ operation NOTIFY operationChanged)
    Q_PROPERTY(QVariantList sourceCandidates READ sourceCandidates NOTIFY sourceCandidatesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)

  public:
    explicit DeviceProvisioningModel(QObject* parent = nullptr);
    [[nodiscard]] QVariantList devices() const;
    [[nodiscard]] QVariantMap topology() const;
    [[nodiscard]] QVariantMap plan() const;
    [[nodiscard]] QVariantMap inspection() const;
    [[nodiscard]] QVariantMap operation() const;
    [[nodiscard]] QVariantList sourceCandidates() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString errorMessage() const;
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void buildPlan(const QVariantMap& selection, const QString& mode);
    Q_INVOKABLE void inspectExistingTarget(const QVariantMap& selection, const QString& passphrase);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void start(
        const QString& profile_id,
        const QString& profile_name,
        const QVariantList& sources,
        const QString& passphrase,
        const QString& confirmation,
        bool automatic_key
    );
    Q_INVOKABLE void poll();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearError();

  signals:
    void devicesChanged();
    void topologyChanged();
    void planChanged();
    void inspectionChanged();
    void operationChanged();
    void sourceCandidatesChanged();
    void stateChanged();
    void completed(const QString& profile_id);

  private:
    enum class RequestKind { Topology,
                             Sources,
                             Inspection,
                             Plan,
                             Start,
                             Poll,
                             Cancel };
    void request(RequestKind kind, const QString& method, const QVariantList& arguments = {});
    bool applyTopology(const QString& payload);
    bool applyPlan(const QString& payload);
    bool applyInspection(const QString& payload);
    bool applySources(const QString& payload);
    bool applyOperation(const QString& payload);
    void setError(const QString& message);

    QDBusConnection bus_;
    QVariantList devices_;
    QVariantMap topology_;
    QVariantMap plan_;
    QVariantMap inspection_;
    QVariantMap operation_;
    QVariantList source_candidates_;
    QString pending_plan_path_;
    QVariantMap pending_inspection_selection_;
    QString error_message_;
    bool busy_ = false;
};

} // namespace btrfsbackup::kde::kcm
