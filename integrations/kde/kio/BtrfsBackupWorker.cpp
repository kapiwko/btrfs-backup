// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BtrfsBackupWorker.hpp"

#include "ManagerApi.hpp"

#include <KIO/Global>
#include <KIO/UDSEntry>

#include <QCoreApplication>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>

#include <algorithm>
#include <chrono>
#include <filesystem>

#include <core/ManagerProtocol.hpp>
#include <platform/linux/storage/LibBtrfsOperations.hpp>
#include <restore/RepositoryDiscoveryService.hpp>

using namespace Qt::StringLiterals;

namespace {
constexpr int maximum_directory_entries = 10000;

class KIOPluginForMetaData : public QObject {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.kio.worker.btrfsbackup" FILE "btrfsbackup.json")
};

KIO::UDSEntry directory_entry(const QString& name, const QString& display_name = {}) {
    KIO::UDSEntry entry;
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, name);
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, display_name.isEmpty() ? name : display_name);
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, u"inode/directory"_s);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IXUSR);
    return entry;
}

std::optional<QString> reply_payload(QDBusPendingCall call) {
    call.waitForFinished();
    QDBusPendingReply<QString> reply(call);
    if (reply.isError())
        return std::nullopt;
    return reply.value();
}

} // namespace

BtrfsBackupWorker::BtrfsBackupWorker(const QByteArray& pool, const QByteArray& app)
    : KIO::ForwardingWorkerBase("btrfsbackup", pool, app) {
}

BtrfsBackupWorker::~BtrfsBackupWorker() {
    close_sessions();
}

std::optional<BtrfsBackupWorker::ParsedUrl> BtrfsBackupWorker::parse(const QUrl& url) {
    if (url.scheme() != u"btrfsbackup"_s || !url.host().isEmpty() || url.hasQuery() || url.hasFragment())
        return std::nullopt;
    const QString decoded = url.path(QUrl::FullyDecoded);
    if (decoded.contains(QChar::Null) || decoded.contains(u"//"_s))
        return std::nullopt;
    const QStringList parts = decoded.split(u'/', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (part == u"."_s || part == u".."_s)
            return std::nullopt;
    }
    return ParsedUrl{
        parts.value(0),
        parts.value(1),
        parts.size() > 2 ? parts.mid(2).join(u'/') : QString{},
    };
}

BtrfsBackupWorker::Session* BtrfsBackupWorker::session(const QString& profile) {
    if (profile.isEmpty())
        return nullptr;
    if (auto existing = sessions_.find(profile); existing != sessions_.end() && QFileInfo::exists(existing->root))
        return &existing.value();

    const auto payload = reply_payload(btrfsbackup::kde::manager_call(
        QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::open_browse_session), {profile}
    ));
    if (!payload)
        return nullptr;
    const auto opened = btrfsbackup::kde::parse_browse_session(*payload);
    if (!opened || opened->profile_id != profile || !opened->read_only)
        return nullptr;

    btrfsbackup::restore::RepositoryDiscoveryService discovery([](const std::filesystem::path& path) {
        const auto metadata = btrfsbackup::platform::linux::storage::read_btrfs_snapshot_metadata(path);
        if (!metadata)
            return std::optional<btrfsbackup::restore::DiscoveredSnapshotMetadata>{};
        return std::optional{btrfsbackup::restore::DiscoveredSnapshotMetadata{
            metadata->is_subvolume, metadata->readonly, metadata->uuid.value(), metadata->received_uuid.value(),
        }};
    });
    try {
        Session value{opened->session_id, opened->root_path, discovery.discover(opened->root_path.toStdString())};
        auto inserted = sessions_.insert(profile, std::move(value));
        return &inserted.value();
    } catch (...) {
        (void)reply_payload(btrfsbackup::kde::manager_call(
            QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::close_browse_session), {opened->session_id}
        ));
        return nullptr;
    }
}

bool BtrfsBackupWorker::rewriteUrl(const QUrl& url, QUrl& local_url) {
    const auto parsed = parse(url);
    if (!parsed || parsed->profile.isEmpty() || parsed->snapshot.isEmpty())
        return false;
    Session* active = session(parsed->profile);
    if (active == nullptr || !active->catalog)
        return false;
    try {
        const auto& snapshot = active->catalog->snapshot(parsed->snapshot.toStdString());
        std::filesystem::path path = active->catalog->root() / snapshot.repository_path.value();
        if (!parsed->relative_path.isEmpty()) {
            const btrfsbackup::restore::RelativeRestorePath relative{parsed->relative_path.toStdString()};
            for (const auto& component : relative.value()) {
                path /= component;
                std::error_code error;
                const auto status = std::filesystem::symlink_status(path, error);
                if (error || std::filesystem::is_symlink(status))
                    return false;
            }
        }
        local_url = QUrl::fromLocalFile(QString::fromStdString(path.string()));
        return true;
    } catch (...) {
        return false;
    }
}

KIO::WorkerResult BtrfsBackupWorker::list_profiles() {
    const auto payload = reply_payload(btrfsbackup::kde::manager_call(
        QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::list_profiles)
    ));
    const auto profiles = payload ? btrfsbackup::kde::parse_profiles(*payload) : std::nullopt;
    if (!profiles)
        return KIO::WorkerResult::fail(KIO::ERR_SERVICE_NOT_AVAILABLE);
    KIO::UDSEntryList entries;
    for (const auto& profile : *profiles)
        entries.push_back(directory_entry(profile.id, profile.name));
    listEntries(entries);
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult BtrfsBackupWorker::list_snapshots(const QString& profile) {
    Session* active = session(profile);
    if (active == nullptr || !active->catalog)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    KIO::UDSEntryList entries;
    int count = 0;
    for (const auto& snapshot : active->catalog->snapshots()) {
        if (wasKilled())
            return KIO::WorkerResult::fail(KIO::ERR_USER_CANCELED);
        if (snapshot.profile_id != profile.toStdString())
            continue;
        if (++count > maximum_directory_entries)
            return KIO::WorkerResult::fail(KIO::ERR_OUT_OF_MEMORY, u"Repository listing exceeds the safe limit"_s);
        auto entry = directory_entry(QString::fromStdString(snapshot.snapshot_id));
        entry.fastInsert(KIO::UDSEntry::UDS_CREATION_TIME,
            std::chrono::duration_cast<std::chrono::seconds>(snapshot.created_at.time_since_epoch()).count());
        entries.push_back(std::move(entry));
    }
    listEntries(entries);
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult BtrfsBackupWorker::listDir(const QUrl& url) {
    const auto parsed = parse(url);
    if (!parsed)
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL);
    if (parsed->profile.isEmpty())
        return list_profiles();
    if (parsed->snapshot.isEmpty())
        return list_snapshots(parsed->profile);
    return list_repository_directory(url);
}

KIO::WorkerResult BtrfsBackupWorker::list_repository_directory(const QUrl& url) {
    QUrl local;
    if (!rewriteUrl(url, local))
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    const QDir directory(local.toLocalFile());
    if (!directory.exists())
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_ENTER_DIRECTORY);
    const QFileInfoList children = directory.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::System,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase
    );
    KIO::UDSEntryList entries;
    int inspected = 0;
    for (const QFileInfo& child : children) {
        if (wasKilled())
            return KIO::WorkerResult::fail(KIO::ERR_USER_CANCELED);
        if (++inspected > maximum_directory_entries)
            return KIO::WorkerResult::fail(KIO::ERR_OUT_OF_MEMORY, u"Directory listing exceeds the safe limit"_s);
        if (child.isSymLink() || (!child.isDir() && !child.isFile()) || child.fileName() == u".incoming"_s)
            continue;
        KIO::UDSEntry entry;
        entry.fastInsert(KIO::UDSEntry::UDS_NAME, child.fileName());
        entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, child.isDir() ? S_IFDIR : S_IFREG);
        entry.fastInsert(KIO::UDSEntry::UDS_SIZE, child.isFile() ? child.size() : 0);
        entry.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, child.lastModified().toSecsSinceEpoch());
        entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, static_cast<long long>(child.permissions()));
        entry.fastInsert(KIO::UDSEntry::UDS_USER, child.owner());
        entry.fastInsert(KIO::UDSEntry::UDS_GROUP, child.group());
        entries.push_back(std::move(entry));
    }
    listEntries(entries);
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult BtrfsBackupWorker::get(const QUrl& url) {
    QUrl local;
    if (!rewriteUrl(url, local) || !QFileInfo(local.toLocalFile()).isFile())
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    return KIO::ForwardingWorkerBase::get(url);
}

KIO::WorkerResult BtrfsBackupWorker::open(const QUrl& url, QIODevice::OpenMode mode) {
    if (mode != QIODevice::ReadOnly)
        return read_only_failure();
    QUrl local;
    if (!rewriteUrl(url, local) || !QFileInfo(local.toLocalFile()).isFile())
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    return KIO::ForwardingWorkerBase::open(url, mode);
}

KIO::WorkerResult BtrfsBackupWorker::stat(const QUrl& url) {
    const auto parsed = parse(url);
    if (!parsed)
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL);
    if (parsed->profile.isEmpty() || parsed->snapshot.isEmpty()) {
        statEntry(directory_entry(u"."_s));
        return KIO::WorkerResult::pass();
    }
    QUrl local;
    if (!rewriteUrl(url, local))
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    const QFileInfo info(local.toLocalFile());
    if (info.isSymLink() || (!info.isDir() && !info.isFile()))
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    return KIO::ForwardingWorkerBase::stat(url);
}

KIO::WorkerResult BtrfsBackupWorker::mimetype(const QUrl& url) {
    const auto parsed = parse(url);
    if (!parsed)
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL);
    if (parsed->profile.isEmpty() || parsed->snapshot.isEmpty()) {
        mimeType(u"inode/directory"_s);
        return KIO::WorkerResult::pass();
    }
    return KIO::ForwardingWorkerBase::mimetype(url);
}

KIO::WorkerResult BtrfsBackupWorker::read_only_failure() {
    return KIO::WorkerResult::fail(KIO::ERR_WRITE_ACCESS_DENIED, u"Backup repositories are read-only"_s);
}

KIO::WorkerResult BtrfsBackupWorker::put(const QUrl&, int, KIO::JobFlags) { return read_only_failure(); }
KIO::WorkerResult BtrfsBackupWorker::mkdir(const QUrl&, int) { return read_only_failure(); }
KIO::WorkerResult BtrfsBackupWorker::rename(const QUrl&, const QUrl&, KIO::JobFlags) { return read_only_failure(); }
KIO::WorkerResult BtrfsBackupWorker::symlink(const QString&, const QUrl&, KIO::JobFlags) { return read_only_failure(); }
KIO::WorkerResult BtrfsBackupWorker::chmod(const QUrl&, int) { return read_only_failure(); }
KIO::WorkerResult BtrfsBackupWorker::chown(const QUrl&, const QString&, const QString&) { return read_only_failure(); }
KIO::WorkerResult BtrfsBackupWorker::setModificationTime(const QUrl&, const QDateTime&) { return read_only_failure(); }
KIO::WorkerResult BtrfsBackupWorker::copy(const QUrl&, const QUrl&, int, KIO::JobFlags) { return read_only_failure(); }
KIO::WorkerResult BtrfsBackupWorker::del(const QUrl&, bool) { return read_only_failure(); }

void BtrfsBackupWorker::close_sessions() noexcept {
    for (const Session& session : std::as_const(sessions_)) {
        try {
            (void)reply_payload(btrfsbackup::kde::manager_call(
                QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::close_browse_session), {session.id}
            ));
        } catch (...) {}
    }
    sessions_.clear();
}

extern "C" int Q_DECL_EXPORT kdemain(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(u"kio_btrfsbackup"_s);
    if (argc != 4)
        return 1;
    BtrfsBackupWorker worker(argv[2], argv[3]);
    worker.dispatchLoop();
    return 0;
}

#include "BtrfsBackupWorker.moc"
