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
#include <restore/RestoreError.hpp>
#include <restore/RestorePlan.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>

#include "BrowseSessionClient.hpp"

namespace btrfsbackup::kde::restore {

class RestoreJob;

class RestoreController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl sourceUrl READ sourceUrl CONSTANT)
    Q_PROPERTY(QString sourceName READ sourceName CONSTANT)
    Q_PROPERTY(QString sourceType READ sourceType NOTIFY sourceDetailsChanged)
    Q_PROPERTY(QString sourceIcon READ sourceIcon NOTIFY sourceDetailsChanged)
    Q_PROPERTY(bool sourceIsDirectory READ sourceIsDirectory NOTIFY sourceDetailsChanged)
    Q_PROPERTY(QString sourceSize READ sourceSize NOTIFY sourceDetailsChanged)
    Q_PROPERTY(QString sourceModified READ sourceModified NOTIFY sourceDetailsChanged)
    Q_PROPERTY(QString snapshotCreated READ snapshotCreated NOTIFY sourceDetailsChanged)
    Q_PROPERTY(bool sourceDetailsAvailable READ sourceDetailsAvailable NOTIFY sourceDetailsChanged)
    Q_PROPERTY(QString destination READ destination WRITE setDestination NOTIFY planChanged)
    Q_PROPERTY(bool replaceExisting READ replaceExisting WRITE setReplaceExisting NOTIFY planChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY stateChanged)
    Q_PROPERTY(QString errorTechnicalDetails READ errorTechnicalDetails NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool completed READ completed NOTIFY stateChanged)
    Q_PROPERTY(bool checkingSpace READ checkingSpace NOTIFY progressChanged)
    Q_PROPERTY(qulonglong restoredFiles READ restoredFiles NOTIFY stateChanged)
    Q_PROPERTY(qulonglong restoredBytes READ restoredBytes NOTIFY stateChanged)
    Q_PROPERTY(QString restoredSize READ restoredSize NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString transferredSize READ transferredSize NOTIFY progressChanged)
    Q_PROPERTY(QString transferSpeed READ transferSpeed NOTIFY progressChanged)
    Q_PROPERTY(QString currentItem READ currentItem NOTIFY progressChanged)

  public:
    explicit RestoreController(QUrl source_url, QObject* parent = nullptr);
    ~RestoreController() noexcept override;

    QUrl sourceUrl() const;
    QString sourceName() const;
    QString sourceType() const;
    QString sourceIcon() const;
    bool sourceIsDirectory() const noexcept;
    QString sourceSize() const;
    QString sourceModified() const;
    QString snapshotCreated() const;
    bool sourceDetailsAvailable() const noexcept;
    QString destination() const;
    void setDestination(const QString& value);
    bool replaceExisting() const;
    void setReplaceExisting(bool value);
    QString errorText() const;
    QString errorCode() const;
    QString errorTechnicalDetails() const;
    bool busy() const;
    bool completed() const;
    bool checkingSpace() const noexcept;
    qulonglong restoredFiles() const noexcept;
    qulonglong restoredBytes() const noexcept;
    QString restoredSize() const;
    double progress() const noexcept;
    QString transferredSize() const;
    QString transferSpeed() const;
    QString currentItem() const;

    Q_INVOKABLE void loadDetails();
    Q_INVOKABLE void chooseDestination();
    Q_INVOKABLE bool confirmOverwrite();
    Q_INVOKABLE void execute();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openRestoredLocation();

  signals:
    void planChanged();
    void stateChanged();
    void sourceDetailsChanged();
    void progressChanged();
    void overwriteConfirmationRequested(const QString& destination);

  private:
    bool prepare_plan();
    void ensure_source_open();
    void clear_error();
    void set_error(btrfsbackup::restore::RestoreErrorCode code, const QString& technical_details);
    void set_unexpected_error(const QString& technical_details);
    void close_session() noexcept;

    QUrl source_url_;
    QString profile_id_;
    QString snapshot_id_;
    QString relative_path_;
    QString source_type_;
    QString source_icon_ = QStringLiteral("unknown");
    bool source_is_directory_ = false;
    qulonglong source_size_bytes_ = 0;
    QString source_size_;
    QString source_modified_;
    QString snapshot_created_;
    QString destination_;
    QString session_id_;
    std::optional<btrfsbackup::kde::BrowseOperationLease> execution_lease_;
    btrfsbackup::platform::linux::OwnedFileDescriptor source_entry_;
    bool replace_existing_ = false;
    bool busy_ = false;
    bool completed_ = false;
    bool checking_space_ = false;
    qulonglong restored_files_ = 0;
    qulonglong restored_bytes_ = 0;
    qulonglong progress_bytes_ = 0;
    qulonglong progress_speed_ = 0;
    QString current_item_;
    QString error_text_;
    QString error_code_;
    QString error_technical_details_;
    std::optional<btrfsbackup::restore::RepositoryCatalog> catalog_;
    std::optional<btrfsbackup::restore::RestorePlan> plan_;
    RestoreJob* job_ = nullptr;
    KUiServerV2JobTracker tracker_;
};

} // namespace btrfsbackup::kde::restore
