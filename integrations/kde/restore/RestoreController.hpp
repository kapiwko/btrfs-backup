// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KUiServerV2JobTracker>

#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>
#include <optional>

#include <restore/RepositoryCatalog.hpp>
#include <restore/RestorePlan.hpp>

namespace btrfsbackup::kde::restore {

class RestoreJob;

class RestoreController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl sourceUrl READ sourceUrl CONSTANT)
    Q_PROPERTY(QString sourceName READ sourceName CONSTANT)
    Q_PROPERTY(QString destination READ destination WRITE setDestination NOTIFY planChanged)
    Q_PROPERTY(bool replaceExisting READ replaceExisting WRITE setReplaceExisting NOTIFY planChanged)
    Q_PROPERTY(QString planSummary READ planSummary NOTIFY planChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool completed READ completed NOTIFY stateChanged)

  public:
    explicit RestoreController(QUrl source_url, QObject* parent = nullptr);
    ~RestoreController() noexcept override;

    QUrl sourceUrl() const;
    QString sourceName() const;
    QString destination() const;
    void setDestination(const QString& value);
    bool replaceExisting() const;
    void setReplaceExisting(bool value);
    QString planSummary() const;
    QString errorText() const;
    bool busy() const;
    bool completed() const;

    Q_INVOKABLE bool preview();
    Q_INVOKABLE void chooseDestination();
    Q_INVOKABLE bool confirmOverwrite();
    Q_INVOKABLE void execute();
    Q_INVOKABLE void cancel();

  signals:
    void planChanged();
    void stateChanged();
    void overwriteConfirmationRequested(const QString& destination);

  private:
    bool prepare_plan();
    void close_session() noexcept;

    QUrl source_url_;
    QString profile_id_;
    QString snapshot_id_;
    QString relative_path_;
    QString destination_;
    QString session_id_;
    QString session_root_;
    bool replace_existing_ = false;
    bool busy_ = false;
    bool completed_ = false;
    QString plan_summary_;
    QString error_text_;
    std::optional<btrfsbackup::restore::RepositoryCatalog> catalog_;
    std::optional<btrfsbackup::restore::RestorePlan> plan_;
    RestoreJob* job_ = nullptr;
    KUiServerV2JobTracker tracker_;
};

} // namespace btrfsbackup::kde::restore
