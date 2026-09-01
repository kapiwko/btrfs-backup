// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KIO/WorkerBase>

#include <QHash>
#include <QUrl>

#include <memory>
#include <optional>

#include <restore/RepositoryCatalog.hpp>

#include "SecureBrowsePath.hpp"

class BtrfsBackupWorker final : public KIO::WorkerBase {
  public:
    BtrfsBackupWorker(const QByteArray& pool, const QByteArray& app);
    ~BtrfsBackupWorker() noexcept override;

    KIO::WorkerResult listDir(const QUrl& url) override;
    KIO::WorkerResult get(const QUrl& url) override;
    KIO::WorkerResult open(const QUrl& url, QIODevice::OpenMode mode) override;
    KIO::WorkerResult read(KIO::filesize_t size) override;
    KIO::WorkerResult seek(KIO::filesize_t offset) override;
    KIO::WorkerResult close() override;
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

  private:
    struct Session {
        QString id;
        std::shared_ptr<btrfsbackup::kde::kio::SecureBrowseFile> root_descriptor;
        std::optional<btrfsbackup::restore::RepositoryCatalog> catalog;
    };
    struct ParsedUrl {
        QString profile;
        QString snapshot;
        QString relative_path;
    };

    static std::optional<ParsedUrl> parse(const QUrl& url);
    Session* session(const QString& profile);
    [[nodiscard]] std::optional<std::filesystem::path> resolve_entry(
        const ParsedUrl& url,
        const Session& session
    ) const;
    [[nodiscard]] bool session_root_available(const Session& session) const;
    void close_open_file() noexcept;
    KIO::WorkerResult list_profiles();
    KIO::WorkerResult list_snapshots(const QString& profile);
    KIO::WorkerResult list_repository_directory(const QUrl& url);
    KIO::WorkerResult list_versions(const ParsedUrl& url);
    static KIO::WorkerResult read_only_failure();
    void close_sessions() noexcept;

    QHash<QString, Session> sessions_;
    btrfsbackup::kde::kio::SecureBrowseFile open_file_;
    QString open_session_id_;
};
