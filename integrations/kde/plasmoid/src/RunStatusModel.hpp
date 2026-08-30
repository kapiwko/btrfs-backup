// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

class RunStatusModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString runId READ runId NOTIFY changed)
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(QString activity READ activity NOTIFY changed)
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY changed)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY changed)
    Q_PROPERTY(QString targetName READ targetName NOTIFY changed)
    Q_PROPERTY(qint64 speedBps READ speedBps NOTIFY changed)
    Q_PROPERTY(QString speedText READ speedText NOTIFY changed)
    Q_PROPERTY(int etaSeconds READ etaSeconds NOTIFY changed)
    Q_PROPERTY(int sourceProgress READ sourceProgress NOTIFY changed)
    Q_PROPERTY(int overallProgress READ overallProgress NOTIFY changed)
    Q_PROPERTY(QString progressAccuracy READ progressAccuracy NOTIFY changed)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY changed)

  public:
    explicit RunStatusModel(QObject* parent = nullptr);

    QString state() const;
    QString runId() const;
    QString phase() const;
    QString activity() const;
    bool canCancel() const;
    QString sourceName() const;
    QString targetName() const;
    qint64 speedBps() const;
    QString speedText() const;
    int etaSeconds() const;
    int sourceProgress() const;
    int overallProgress() const;
    QString progressAccuracy() const;
    QString errorCode() const;

    void setCancelSupported(bool supported);
    [[nodiscard]] bool apply(const QString& payload);
    void reset();

  signals:
    void changed();
    void activeRunFinished();

  private:
    bool cancel_supported_ = false;
    QString run_id_;
    QString state_ = QStringLiteral("unknown");
    QString phase_ = QStringLiteral("idle");
    QString activity_ = QStringLiteral("idle");
    bool can_cancel_ = false;
    QString source_name_;
    QString target_name_;
    qint64 speed_bps_ = 0;
    int eta_seconds_ = -1;
    int source_progress_ = -1;
    int overall_progress_ = -1;
    QString progress_accuracy_ = QStringLiteral("indeterminate");
    QString error_code_;
};
