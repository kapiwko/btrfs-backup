// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KIO/ForwardingWorkerBase>

#include <QHash>
#include <QUrl>

#include <optional>

#include <restore/RepositoryCatalog.hpp>

class BtrfsBackupWorker final : public KIO::ForwardingWorkerBase {
    Q_OBJECT

  public:
    BtrfsBackupWorker(const QByteArray& pool, const QByteArray& app);
    ~BtrfsBackupWorker() override;

    KIO::WorkerResult listDir(const QUrl& url) override;
    KIO::WorkerResult get(const QUrl& url) override;
    KIO::WorkerResult open(const QUrl& url, QIODevice::OpenMode mode) override;
    KIO::WorkerResult stat(const QUrl& url) override;
    KIO::WorkerResult mimetype(const QUrl& url) override;
    KIO::WorkerResult put(const QUrl&, int, KIO::JobFlags) override;
    KIO::WorkerResult mkdir(const QUrl&, int) override;
    KIO::WorkerResult rename(const QUrl&, const QUrl&, KIO::JobFlags) override;
    KIO::WorkerResult symlink(const QString&, const QUrl&, KIO::JobFlags) override;
    KIO::WorkerResult chmod(const QUrl&, int) override;
    KIO::WorkerResult chown(const QUrl&, const QString&, const QString&) override;
    KIO::WorkerResult setModificationTime(const QUrl&, const QDateTime&) override;
    KIO::WorkerResult copy(const QUrl&, const QUrl&, int, KIO::JobFlags) override;
    KIO::WorkerResult del(const QUrl&, bool) override;

  protected:
    bool rewriteUrl(const QUrl& url, QUrl& local_url) override;

  private:
    struct Session {
        QString id;
        QString root;
        std::optional<btrfsbackup::restore::RepositoryCatalog> catalog;
    };
    struct ParsedUrl {
        QString profile;
        QString snapshot;
        QString relative_path;
    };

    static std::optional<ParsedUrl> parse(const QUrl& url);
    Session* session(const QString& profile);
    KIO::WorkerResult list_profiles();
    KIO::WorkerResult list_snapshots(const QString& profile);
    KIO::WorkerResult list_repository_directory(const QUrl& url);
    static KIO::WorkerResult read_only_failure();
    void close_sessions() noexcept;

    QHash<QString, Session> sessions_;
};
