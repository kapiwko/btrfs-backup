// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BtrfsBackupWorker.hpp"

#include "ManagerApi.hpp"

#include <KIO/Global>
#include <KIO/UDSEntry>

#include <QCoreApplication>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <limits>

#include <core/ManagerProtocol.hpp>
#include <platform/linux/storage/LibBtrfsOperations.hpp>
#include <restore/RepositoryDiscoveryService.hpp>

using Qt::StringLiterals::operator""_s;

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

bool set_session_active(const QString& session_id, bool active) {
    return reply_payload(btrfsbackup::kde::manager_call(QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::set_browse_session_active), {session_id, active})).has_value();
}

class BrowseSessionPin final {
  public:
    explicit BrowseSessionPin(const QString& session_id)
        : session_id_(session_id), active_(set_session_active(session_id_, true)) {
    }
    ~BrowseSessionPin() noexcept {
        if (active_) {
            try {
                (void)set_session_active(session_id_, false);
            } catch (...) {}
        }
    }
    BrowseSessionPin(const BrowseSessionPin&) = delete;
    BrowseSessionPin& operator=(const BrowseSessionPin&) = delete;
    BrowseSessionPin(BrowseSessionPin&&) = delete;
    BrowseSessionPin& operator=(BrowseSessionPin&&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept {
        return active_;
    }

  private:
    QString session_id_;
    bool active_;
};

} // namespace

BtrfsBackupWorker::BtrfsBackupWorker(const QByteArray& pool, const QByteArray& app)
    : KIO::WorkerBase("btrfsbackup", pool, app) {
}

BtrfsBackupWorker::~BtrfsBackupWorker() noexcept {
    close_open_file();
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
    if (auto existing = sessions_.find(profile); existing != sessions_.end()) {
        const auto renewed = reply_payload(btrfsbackup::kde::manager_call(QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::renew_browse_session), {existing->id}));
        const auto lease = renewed ? btrfsbackup::kde::parse_browse_session(*renewed) : std::nullopt;
        if (lease && lease->session_id == existing->id && session_root_available(existing.value()))
            return &existing.value();
        (void)reply_payload(btrfsbackup::kde::manager_call(QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::close_browse_session), {existing->id}));
        sessions_.erase(existing);
    }

    const auto payload = reply_payload(btrfsbackup::kde::manager_call(QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::open_browse_session), {profile}));
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
            metadata->is_subvolume,
            metadata->readonly,
            metadata->uuid.value(),
            metadata->received_uuid.value(),
        }};
    });
    try {
        auto root_descriptor = std::make_shared<btrfsbackup::kde::kio::SecureBrowseFile>(
            btrfsbackup::kde::kio::open_browse_directory(opened->root_path.toStdString(), {})
        );
        const std::filesystem::path stable_root =
            "/proc/self/fd/" + std::to_string(root_descriptor->descriptor()) + "/.";
        Session value{
            opened->session_id,
            root_descriptor,
            discovery.discover(stable_root),
        };
        auto inserted = sessions_.insert(profile, std::move(value));
        return &inserted.value();
    } catch (...) {
        (void)reply_payload(btrfsbackup::kde::manager_call(QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::close_browse_session), {opened->session_id}));
        return nullptr;
    }
}

std::optional<std::filesystem::path> BtrfsBackupWorker::resolve_entry(
    const ParsedUrl& url,
    const Session& session
) const {
    if (!session.catalog || url.snapshot.isEmpty())
        return std::nullopt;
    try {
        const auto& snapshot = session.catalog->snapshot(url.snapshot.toStdString());
        std::filesystem::path relative = snapshot.repository_path.value();
        if (!url.relative_path.isEmpty()) {
            const btrfsbackup::restore::RelativeRestorePath requested{url.relative_path.toStdString()};
            for (const auto& component : requested.value())
                relative /= component;
        }
        return relative.lexically_normal();
    } catch (...) {
        return std::nullopt;
    }
}

bool BtrfsBackupWorker::session_root_available(const Session& session) const {
    return session.root_descriptor && session.root_descriptor->valid();
}

KIO::WorkerResult BtrfsBackupWorker::list_profiles() {
    const auto payload = reply_payload(btrfsbackup::kde::manager_call(QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::list_profiles)));
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
    const BrowseSessionPin pin(active->id);
    if (!pin)
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
        entry.fastInsert(KIO::UDSEntry::UDS_CREATION_TIME, std::chrono::duration_cast<std::chrono::seconds>(snapshot.created_at.time_since_epoch()).count());
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
    if (parsed->snapshot == u".versions"_s)
        return list_versions(*parsed);
    return list_repository_directory(url);
}

KIO::WorkerResult BtrfsBackupWorker::list_versions(const ParsedUrl& url) {
    const QStringList parts = url.relative_path.split(u'/', Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL);
    const QString& source_id = parts.front();
    const QString relative = parts.size() > 1 ? parts.mid(1).join(u'/') : u"."_s;
    Session* active = session(url.profile);
    if (active == nullptr || !active->catalog)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const BrowseSessionPin pin(active->id);
    if (!pin)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    KIO::UDSEntryList entries;
    for (const auto& snapshot : active->catalog->snapshots()) {
        if (wasKilled())
            return KIO::WorkerResult::fail(KIO::ERR_USER_CANCELED);
        if (snapshot.profile_id != url.profile.toStdString() || snapshot.source_id != source_id.toStdString())
            continue;
        bool valid = true;
        try {
            std::filesystem::path candidate = snapshot.repository_path.value();
            const btrfsbackup::restore::RelativeRestorePath path{relative.toStdString()};
            for (const auto& component : path.value())
                candidate /= component;
            (void)btrfsbackup::kde::kio::open_browse_metadata(
                active->root_descriptor->descriptor(),
                candidate.lexically_normal()
            );
        } catch (...) {
            valid = false;
        }
        if (!valid)
            continue;
        auto entry = directory_entry(QString::fromStdString(snapshot.snapshot_id));
        entry.fastInsert(KIO::UDSEntry::UDS_CREATION_TIME, std::chrono::duration_cast<std::chrono::seconds>(snapshot.created_at.time_since_epoch()).count());
        QUrl target;
        target.setScheme(u"btrfsbackup"_s);
        QString target_path = u"/"_s + url.profile + u"/"_s + QString::fromStdString(snapshot.snapshot_id);
        if (relative != u"."_s)
            target_path += u"/"_s + relative;
        target.setPath(target_path);
        entry.fastInsert(KIO::UDSEntry::UDS_URL, target.toString(QUrl::FullyEncoded));
        entries.push_back(std::move(entry));
    }
    listEntries(entries);
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult BtrfsBackupWorker::list_repository_directory(const QUrl& url) {
    const auto parsed = parse(url);
    Session* active = parsed ? session(parsed->profile) : nullptr;
    if (active == nullptr)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const BrowseSessionPin pin(active->id);
    if (!pin)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const auto relative = resolve_entry(*parsed, *active);
    if (!relative)
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    KIO::UDSEntryList entries;
    try {
        const auto directory = btrfsbackup::kde::kio::open_browse_directory(
            active->root_descriptor->descriptor(),
            *relative
        );
        auto children = btrfsbackup::kde::kio::list_browse_directory(
            directory.descriptor(),
            maximum_directory_entries
        );
        std::ranges::sort(children, {}, &btrfsbackup::kde::kio::BrowseDirectoryEntry::name);
        for (const auto& child : children) {
            if (wasKilled())
                return KIO::WorkerResult::fail(KIO::ERR_USER_CANCELED);
            KIO::UDSEntry entry;
            entry.fastInsert(KIO::UDSEntry::UDS_NAME, QString::fromStdString(child.name));
            entry.fastInsert(
                KIO::UDSEntry::UDS_FILE_TYPE,
                child.kind == btrfsbackup::kde::kio::BrowseEntryKind::Directory ? S_IFDIR : S_IFREG
            );
            entry.fastInsert(KIO::UDSEntry::UDS_SIZE, static_cast<KIO::filesize_t>(child.size));
            entry.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, child.modified_at);
            entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, static_cast<long long>(child.mode & 0777));
            entries.push_back(std::move(entry));
        }
    } catch (const btrfsbackup::kde::kio::BrowseDirectoryLimitError&) {
        return KIO::WorkerResult::fail(KIO::ERR_OUT_OF_MEMORY, u"Directory listing exceeds the safe limit"_s);
    } catch (...) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_ENTER_DIRECTORY);
    }
    listEntries(entries);
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult BtrfsBackupWorker::get(const QUrl& url) {
    const auto parsed = parse(url);
    Session* active = parsed ? session(parsed->profile) : nullptr;
    if (active == nullptr)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const BrowseSessionPin pin(active->id);
    if (!pin)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const auto relative = resolve_entry(*parsed, *active);
    if (!relative)
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    try {
        auto file = btrfsbackup::kde::kio::open_browse_regular_file(
            active->root_descriptor->descriptor(),
            *relative
        );
        struct stat status{};
        if (fstat(file.descriptor(), &status) != 0)
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ);
        mimeType(QMimeDatabase{}.mimeTypeForFile(QString::fromStdString(relative->filename().string()), QMimeDatabase::MatchExtension).name());
        totalSize(status.st_size);
        KIO::filesize_t processed = 0;
        QByteArray buffer(128 * 1024, Qt::Uninitialized);
        while (true) {
            if (wasKilled())
                return KIO::WorkerResult::fail(KIO::ERR_USER_CANCELED);
            const ssize_t count = ::read(file.descriptor(), buffer.data(), static_cast<std::size_t>(buffer.size()));
            if (count < 0) {
                if (errno == EINTR)
                    continue;
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ);
            }
            if (count == 0)
                break;
            data(QByteArray(buffer.constData(), count));
            processed += count;
            processedSize(processed);
        }
        data({});
        return KIO::WorkerResult::pass();
    } catch (...) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    }
}

KIO::WorkerResult BtrfsBackupWorker::open(const QUrl& url, QIODevice::OpenMode mode) {
    if (mode != QIODevice::ReadOnly)
        return read_only_failure();
    close_open_file();
    const auto parsed = parse(url);
    Session* active = parsed ? session(parsed->profile) : nullptr;
    if (active == nullptr)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    if (!set_session_active(active->id, true))
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const auto relative = resolve_entry(*parsed, *active);
    if (!relative) {
        (void)set_session_active(active->id, false);
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    }
    try {
        open_file_ = btrfsbackup::kde::kio::open_browse_regular_file(
            active->root_descriptor->descriptor(),
            *relative
        );
        open_session_id_ = active->id;
        struct stat status{};
        if (fstat(open_file_.descriptor(), &status) != 0) {
            close_open_file();
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ);
        }
        totalSize(status.st_size);
        return KIO::WorkerResult::pass();
    } catch (...) {
        (void)set_session_active(active->id, false);
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    }
}

KIO::WorkerResult BtrfsBackupWorker::read(KIO::filesize_t size) {
    if (!open_file_.valid())
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ);
    const auto requested = static_cast<qsizetype>(std::min<KIO::filesize_t>(
        size,
        std::min<KIO::filesize_t>(1024 * 1024, std::numeric_limits<qsizetype>::max())
    ));
    QByteArray buffer(requested, Qt::Uninitialized);
    ssize_t count;
    do {
        count = ::read(open_file_.descriptor(), buffer.data(), static_cast<std::size_t>(buffer.size()));
    } while (count < 0 && errno == EINTR);
    if (count < 0)
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ);
    data(QByteArray(buffer.constData(), count));
    const off_t current = lseek(open_file_.descriptor(), 0, SEEK_CUR);
    if (current >= 0)
        position(current);
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult BtrfsBackupWorker::seek(KIO::filesize_t offset) {
    if (!open_file_.valid())
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_SEEK);
    const off_t result = lseek(open_file_.descriptor(), static_cast<off_t>(offset), SEEK_SET);
    if (result < 0)
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_SEEK);
    position(result);
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult BtrfsBackupWorker::close() {
    close_open_file();
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult BtrfsBackupWorker::stat(const QUrl& url) {
    const auto parsed = parse(url);
    if (!parsed)
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL);
    if (parsed->profile.isEmpty() || parsed->snapshot.isEmpty() || parsed->snapshot == u".versions"_s) {
        statEntry(directory_entry(u"."_s));
        return KIO::WorkerResult::pass();
    }
    Session* active = session(parsed->profile);
    if (active == nullptr)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const BrowseSessionPin pin(active->id);
    if (!pin)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const auto relative = resolve_entry(*parsed, *active);
    if (!relative)
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    struct stat status{};
    try {
        const auto entry = btrfsbackup::kde::kio::open_browse_metadata(
            active->root_descriptor->descriptor(),
            *relative
        );
        if (fstat(entry.descriptor(), &status) != 0)
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    } catch (...) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    }
    KIO::UDSEntry result;
    result.fastInsert(KIO::UDSEntry::UDS_NAME, relative->filename().empty() ? u"."_s : QString::fromStdString(relative->filename().string()));
    result.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_ISDIR(status.st_mode) ? S_IFDIR : S_IFREG);
    result.fastInsert(KIO::UDSEntry::UDS_SIZE, S_ISREG(status.st_mode) ? status.st_size : 0);
    result.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, status.st_mtim.tv_sec);
    result.fastInsert(KIO::UDSEntry::UDS_ACCESS, static_cast<long long>(status.st_mode & 0777));
    statEntry(result);
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult BtrfsBackupWorker::mimetype(const QUrl& url) {
    const auto parsed = parse(url);
    if (!parsed)
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL);
    if (parsed->profile.isEmpty() || parsed->snapshot.isEmpty() || parsed->snapshot == u".versions"_s) {
        mimeType(u"inode/directory"_s);
        return KIO::WorkerResult::pass();
    }
    Session* active = session(parsed->profile);
    if (active == nullptr)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const BrowseSessionPin pin(active->id);
    if (!pin)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const auto relative = resolve_entry(*parsed, *active);
    if (!relative)
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    try {
        const auto entry = btrfsbackup::kde::kio::open_browse_metadata(
            active->root_descriptor->descriptor(),
            *relative
        );
        struct stat status{};
        if (fstat(entry.descriptor(), &status) != 0)
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
        mimeType(S_ISDIR(status.st_mode) ? u"inode/directory"_s : QMimeDatabase{}.mimeTypeForFile(QString::fromStdString(relative->filename().string()), QMimeDatabase::MatchExtension).name());
        return KIO::WorkerResult::pass();
    } catch (...) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    }
}

KIO::WorkerResult BtrfsBackupWorker::read_only_failure() {
    return KIO::WorkerResult::fail(KIO::ERR_WRITE_ACCESS_DENIED, u"Backup repositories are read-only"_s);
}

KIO::WorkerResult BtrfsBackupWorker::put(const QUrl&, int, KIO::JobFlags) {
    return read_only_failure();
}
KIO::WorkerResult BtrfsBackupWorker::mkdir(const QUrl&, int) {
    return read_only_failure();
}
KIO::WorkerResult BtrfsBackupWorker::rename(const QUrl&, const QUrl&, KIO::JobFlags) {
    return read_only_failure();
}
KIO::WorkerResult BtrfsBackupWorker::symlink(const QString&, const QUrl&, KIO::JobFlags) {
    return read_only_failure();
}
KIO::WorkerResult BtrfsBackupWorker::chmod(const QUrl&, int) {
    return read_only_failure();
}
KIO::WorkerResult BtrfsBackupWorker::chown(const QUrl&, const QString&, const QString&) {
    return read_only_failure();
}
KIO::WorkerResult BtrfsBackupWorker::setModificationTime(const QUrl&, const QDateTime&) {
    return read_only_failure();
}
KIO::WorkerResult BtrfsBackupWorker::copy(const QUrl&, const QUrl&, int, KIO::JobFlags) {
    return read_only_failure();
}
KIO::WorkerResult BtrfsBackupWorker::del(const QUrl&, bool) {
    return read_only_failure();
}

void BtrfsBackupWorker::close_open_file() noexcept {
    open_file_ = btrfsbackup::kde::kio::SecureBrowseFile{};
    if (open_session_id_.isEmpty())
        return;
    const QString session_id = std::move(open_session_id_);
    open_session_id_.clear();
    try {
        (void)set_session_active(session_id, false);
    } catch (...) {}
}

void BtrfsBackupWorker::close_sessions() noexcept {
    for (const Session& session : std::as_const(sessions_)) {
        try {
            (void)reply_payload(btrfsbackup::kde::manager_call(QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::close_browse_session), {session.id}));
        } catch (...) {}
    }
    sessions_.clear();
}

extern "C" int Q_DECL_EXPORT kdemain(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"kio_btrfsbackup"_s);
    if (argc != 4)
        return 1;
    BtrfsBackupWorker worker(argv[2], argv[3]);
    worker.dispatchLoop();
    return 0;
}

#include "BtrfsBackupWorker.moc"
