// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KJob>

#include <QString>

#include <functional>

namespace btrfsbackup::kde::monitor {

class BackupProgressJob final : public KJob {
    Q_OBJECT

  public:
    using CancelRequest = std::function<void(const QString&, const QString&)>;

    BackupProgressJob(
        QString profile_id,
        QString run_id,
        QString profile_name,
        QString operation_kind,
        CancelRequest cancel_request,
        QObject* parent = nullptr
    );

    void start() override;
    void update(
        int progress,
        qint64 speed_bps,
        bool can_cancel,
        const QString& activity,
        const QString& source_name,
        const QString& target_name
    );
    void finish_successfully();
    void finish_with_error(const QString& message);
    void finish_cancelled();
    void stop_tracking();
    void cancellation_rejected();

    [[nodiscard]] QString profile_id() const;
    [[nodiscard]] QString run_id() const;
    [[nodiscard]] bool cancellation_requested() const;

  protected:
    bool doKill() override;

  private:
    void publish_description();
    void update_capabilities();

    QString profile_id_;
    QString run_id_;
    QString profile_name_;
    QString operation_kind_;
    QString source_name_;
    QString target_name_;
    QString activity_;
    CancelRequest cancel_request_;
    bool can_cancel_ = false;
    bool cancel_requested_ = false;
    bool started_ = false;
    bool finished_ = false;
};

} // namespace btrfsbackup::kde::monitor
