// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BtrfsBackupWorker.hpp"

#include "BrowseSessionClient.hpp"
#include "ManagerApi.hpp"

#include <KIO/Global>
#include <KIO/UDSEntry>
#include <KLocalizedString>

#include <QCoreApplication>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeDatabase>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <limits>

#include <core/ManagerProtocol.hpp>
#include <restore/RepositoryCatalog.hpp>

using Qt::StringLiterals::operator""_s;

namespace {
struct RemoteEntry {
    QString name;
    bool directory = false;
    std::uint64_t size = 0;
    std::uint32_t mode = 0;
    std::int64_t modified_at = 0;
};

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

QString entry_mime_type(const RemoteEntry& entry) {
    if (entry.directory)
        return u"inode/directory"_s;
    return QMimeDatabase{}.mimeTypeForFile(entry.name, QMimeDatabase::MatchExtension).name();
}

std::optional<QString> reply_payload(QDBusPendingCall call) {
    call.waitForFinished();
    QDBusPendingReply<QString> reply(call);
    if (reply.isError())
        return std::nullopt;
    return reply.value();
}

std::optional<RemoteEntry> parse_remote_entry(const QJsonObject& object) {
    const QJsonValue name = object.value(u"name"_s);
    const QJsonValue kind = object.value(u"kind"_s);
    const QJsonValue size = object.value(u"size"_s);
    const QJsonValue mode = object.value(u"mode"_s);
    const QJsonValue modified = object.value(u"modifiedAt"_s);
    if (!name.isString() || (kind != u"directory"_s && kind != u"file"_s) || !size.isDouble() ||
        !mode.isDouble() || !modified.isDouble())
        return std::nullopt;
    return RemoteEntry{
        name.toString(),
        kind == u"directory"_s,
        static_cast<std::uint64_t>(size.toDouble()),
        static_cast<std::uint32_t>(mode.toDouble()),
        static_cast<std::int64_t>(modified.toDouble()),
    };
}

struct RemoteDirectoryPage {
    std::vector<RemoteEntry> entries;
    QString continuation_token;
};

std::optional<RemoteDirectoryPage> remote_directory_page(
    const QString& session_id,
    const QString& path,
    const QString& continuation_token
) {
    constexpr uint page_size = 512;
    const auto payload = btrfsbackup::kde::BrowseSessionClient{}.listDirectoryPage(
        session_id,
        path,
        continuation_token,
        page_size
    );
    if (!payload)
        return std::nullopt;
    const QJsonDocument document = QJsonDocument::fromJson(payload->toUtf8());
    const QJsonObject root = document.object();
    if (root.value(u"schemaVersion"_s).toInt() != 1 || !root.value(u"entries"_s).isArray() ||
        !root.value(u"continuationToken"_s).isString())
        return std::nullopt;
    RemoteDirectoryPage result;
    for (const QJsonValue& value : root.value(u"entries"_s).toArray()) {
        if (!value.isObject())
            return std::nullopt;
        auto entry = parse_remote_entry(value.toObject());
        if (!entry)
            return std::nullopt;
        result.entries.push_back(std::move(*entry));
    }
    result.continuation_token = root.value(u"continuationToken"_s).toString();
    return result;
}

std::optional<RemoteEntry> remote_entry(const QString& session_id, const QString& path) {
    const auto payload = btrfsbackup::kde::BrowseSessionClient{}.inspectEntry(session_id, path);
    if (!payload)
        return std::nullopt;
    const QJsonDocument document = QJsonDocument::fromJson(payload->toUtf8());
    const QJsonObject object = document.object();
    if (object.value(u"schemaVersion"_s).toInt() != 1)
        return std::nullopt;
    return parse_remote_entry(object);
}

std::optional<btrfsbackup::kde::kio::PreviousVersionsPage> remote_previous_versions(
    const QString& session_id,
    const QString& profile_id,
    const QString& source_id,
    const QString& relative_path,
    const QString& continuation_token,
    QString& error_name
) {
    constexpr uint page_size = 512;
    btrfsbackup::kde::BrowseSessionClient client;
    const auto payload = client.listPreviousVersions(
        session_id,
        profile_id,
        source_id,
        relative_path,
        continuation_token,
        page_size
    );
    error_name = client.lastErrorName();
    return payload ? btrfsbackup::kde::kio::parse_previous_versions_page(*payload) : std::nullopt;
}

std::optional<QHash<QString, btrfsbackup::kde::kio::RepositorySnapshot>> remote_snapshots(const QString& session_id) {
    const auto payload = btrfsbackup::kde::BrowseSessionClient{}.inspectRepository(session_id);
    return payload ? btrfsbackup::kde::kio::parse_repository_snapshots(*payload) : std::nullopt;
}

btrfsbackup::kde::kio::SecureBrowseFile remote_file(const QString& session_id, const QString& path) {
    QDBusPendingReply<QDBusUnixFileDescriptor> reply(btrfsbackup::kde::manager_call(QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::open_browse_file), {session_id, path}));
    reply.waitForFinished();
    if (reply.isError() || !reply.value().isValid())
        return {};
    return btrfsbackup::kde::kio::SecureBrowseFile(dup(reply.value().fileDescriptor()));
}

bool set_session_active(const QString& session_id, bool active) {
    return btrfsbackup::kde::BrowseSessionClient{}.setActive(session_id, active);
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
    session_error_name_.clear();
    if (profile.isEmpty())
        return nullptr;
    if (auto existing = sessions_.find(profile); existing != sessions_.end()) {
        btrfsbackup::kde::BrowseSessionClient browse_sessions;
        const auto lease = browse_sessions.renew(existing->id);
        if (lease && lease->session_id == existing->id)
            return &existing.value();
        (void)browse_sessions.close(existing->id);
        sessions_.erase(existing);
    }

    btrfsbackup::kde::BrowseSessionClient browse_sessions;
    const auto opened = browse_sessions.open(profile);
    session_error_name_ = browse_sessions.lastErrorName();
    if (!opened || opened->profile_id != profile || !opened->read_only)
        return nullptr;

    auto snapshots = remote_snapshots(opened->session_id);
    if (!snapshots) {
        (void)browse_sessions.close(opened->session_id);
        return nullptr;
    }
    auto inserted = sessions_.insert(profile, Session{opened->session_id, std::move(*snapshots)});
    return &inserted.value();
}

std::optional<std::filesystem::path> BtrfsBackupWorker::resolve_entry(
    const ParsedUrl& url,
    const Session& session
) const {
    if (url.snapshot.isEmpty())
        return std::nullopt;
    try {
        const auto snapshot = session.snapshots.constFind(url.snapshot);
        if (snapshot == session.snapshots.cend() || !snapshot->verified || snapshot->profile_id != url.profile)
            return std::nullopt;
        const btrfsbackup::restore::RelativeRestorePath root_entry{snapshot->repository_path.toStdString()};
        std::filesystem::path relative = root_entry.value();
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

KIO::WorkerResult BtrfsBackupWorker::session_failure() const {
    if (session_error_name_.endsWith(QStringLiteral(".TargetUnavailable"))) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_MOUNT,
            i18nd(
                "plasma_applet_org.btrfsbackup.plasmoid",
                "The backup device is disconnected or unavailable."
            )
        );
    }
    if (session_error_name_.endsWith(QStringLiteral(".Busy")))
        return KIO::WorkerResult::fail(KIO::ERR_SERVER_TIMEOUT);
    if (session_error_name_.endsWith(QStringLiteral(".NotAuthorized")))
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    return KIO::WorkerResult::fail(
        KIO::ERR_CANNOT_ENTER_DIRECTORY,
        i18nd(
            "plasma_applet_org.btrfsbackup.plasmoid",
            "The backup repository could not be read."
        )
    );
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
    if (active == nullptr)
        return session_failure();
    const BrowseSessionPin pin(active->id);
    if (!pin)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    KIO::UDSEntryList entries;
    std::vector<btrfsbackup::kde::kio::RepositorySnapshot> snapshots;
    for (const auto& snapshot : std::as_const(active->snapshots))
        if (snapshot.verified && snapshot.profile_id == profile)
            snapshots.push_back(snapshot);
    std::ranges::sort(snapshots, std::greater{}, &btrfsbackup::kde::kio::RepositorySnapshot::created_at);
    for (const auto& snapshot : snapshots) {
        if (wasKilled())
            return KIO::WorkerResult::fail(KIO::ERR_USER_CANCELED);
        auto entry = directory_entry(snapshot.id, QLocale{}.toString(snapshot.created_at.toLocalTime(), QLocale::ShortFormat));
        entry.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, snapshot.created_at.toSecsSinceEpoch());
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
    Session* active = session(url.profile);
    if (active == nullptr)
        return session_failure();
    const BrowseSessionPin pin(active->id);
    if (!pin)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const QStringList parts = url.relative_path.split(u'/', Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL);
    const QString source_id = parts.front();
    const QString requested = parts.size() > 1 ? parts.mid(1).join(u'/') : u"."_s;
    QString continuation_token;
    QString error_name;
    bool first_page = true;
    do {
        const auto page = remote_previous_versions(
            active->id,
            url.profile,
            source_id,
            requested,
            continuation_token,
            error_name
        );
        if (!page) {
            if (first_page && btrfsbackup::kde::kio::previous_versions_method_unavailable(error_name))
                break;
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_ENTER_DIRECTORY);
        }
        if (!page->continuation_token.isEmpty() && page->continuation_token == continuation_token)
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_ENTER_DIRECTORY);
        KIO::UDSEntryList entries;
        for (const auto& version : page->entries) {
            if (wasKilled())
                return KIO::WorkerResult::fail(KIO::ERR_USER_CANCELED);
            const auto target = btrfsbackup::kde::kio::version_target_url(url.profile, version.snapshot_id, requested);
            if (!target)
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_ENTER_DIRECTORY);
            const RemoteEntry remote{
                requested == u"."_s ? version.snapshot_id : requested.section(u'/', -1),
                version.directory,
                version.size,
                version.mode,
                version.modified_at,
            };
            KIO::UDSEntry entry;
            entry.fastInsert(KIO::UDSEntry::UDS_NAME, version.snapshot_id);
            entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, QLocale{}.toString(version.created_at.toLocalTime(), QLocale::ShortFormat));
            entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, version.directory ? S_IFDIR : S_IFREG);
            entry.fastInsert(KIO::UDSEntry::UDS_SIZE, static_cast<KIO::filesize_t>(version.size));
            entry.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, version.created_at.toSecsSinceEpoch());
            entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, static_cast<long long>(version.mode & 0777));
            entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, entry_mime_type(remote));
            entry.fastInsert(KIO::UDSEntry::UDS_URL, target->toString(QUrl::FullyEncoded));
            entries.push_back(std::move(entry));
        }
        listEntries(entries);
        continuation_token = page->continuation_token;
        first_page = false;
    } while (!continuation_token.isEmpty());
    if (!first_page)
        return KIO::WorkerResult::pass();

    const auto snapshots = btrfsbackup::kde::kio::matching_versions(active->snapshots, url.profile, source_id);
    KIO::UDSEntryList entries;
    for (const auto& snapshot : snapshots) {
        if (wasKilled())
            return KIO::WorkerResult::fail(KIO::ERR_USER_CANCELED);
        std::filesystem::path repository_entry{snapshot.repository_path.toStdString()};
        if (requested != u"."_s)
            repository_entry /= requested.toStdString();
        const auto remote = remote_entry(active->id, QString::fromStdString(repository_entry.string()));
        if (!remote)
            continue;
        const auto target = btrfsbackup::kde::kio::version_target_url(url.profile, snapshot.id, requested);
        if (!target)
            continue;
        KIO::UDSEntry entry;
        entry.fastInsert(KIO::UDSEntry::UDS_NAME, snapshot.id);
        entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, QLocale{}.toString(snapshot.created_at.toLocalTime(), QLocale::ShortFormat));
        entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, remote->directory ? S_IFDIR : S_IFREG);
        entry.fastInsert(KIO::UDSEntry::UDS_SIZE, static_cast<KIO::filesize_t>(remote->size));
        entry.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, snapshot.created_at.toSecsSinceEpoch());
        entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, static_cast<long long>(remote->mode & 0777));
        entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, entry_mime_type(*remote));
        entry.fastInsert(KIO::UDSEntry::UDS_URL, target->toString(QUrl::FullyEncoded));
        entries.push_back(std::move(entry));
    }
    listEntries(entries);
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult BtrfsBackupWorker::list_repository_directory(const QUrl& url) {
    const auto parsed = parse(url);
    Session* active = parsed ? session(parsed->profile) : nullptr;
    if (active == nullptr)
        return session_failure();
    const BrowseSessionPin pin(active->id);
    if (!pin)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const auto relative = resolve_entry(*parsed, *active);
    if (!relative)
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    const QString path = QString::fromStdString(relative->string());
    QString continuation_token;
    do {
        const auto page = remote_directory_page(active->id, path, continuation_token);
        if (!page || (!page->continuation_token.isEmpty() && page->continuation_token == continuation_token))
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_ENTER_DIRECTORY);
        KIO::UDSEntryList entries;
        for (const auto& child : page->entries) {
            if (wasKilled())
                return KIO::WorkerResult::fail(KIO::ERR_USER_CANCELED);
            KIO::UDSEntry entry;
            entry.fastInsert(KIO::UDSEntry::UDS_NAME, child.name);
            entry.fastInsert(
                KIO::UDSEntry::UDS_FILE_TYPE,
                child.directory ? S_IFDIR : S_IFREG
            );
            entry.fastInsert(KIO::UDSEntry::UDS_SIZE, static_cast<KIO::filesize_t>(child.size));
            entry.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, child.modified_at);
            entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, static_cast<long long>(child.mode & 0777));
            entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, entry_mime_type(child));
            entries.push_back(std::move(entry));
        }
        listEntries(entries);
        continuation_token = page->continuation_token;
    } while (!continuation_token.isEmpty());
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult BtrfsBackupWorker::get(const QUrl& url) {
    const auto parsed = parse(url);
    Session* active = parsed ? session(parsed->profile) : nullptr;
    if (active == nullptr)
        return session_failure();
    const BrowseSessionPin pin(active->id);
    if (!pin)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const auto relative = resolve_entry(*parsed, *active);
    if (!relative)
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    try {
        auto file = remote_file(active->id, QString::fromStdString(relative->string()));
        if (!file.valid())
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
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
        return session_failure();
    if (!set_session_active(active->id, true))
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const auto relative = resolve_entry(*parsed, *active);
    if (!relative) {
        (void)set_session_active(active->id, false);
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    }
    try {
        open_file_ = remote_file(active->id, QString::fromStdString(relative->string()));
        if (!open_file_.valid())
            throw std::runtime_error("cannot open remote browse file");
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
        const QString name = parsed->profile.isEmpty()
            ? u"."_s
            : parsed->snapshot.isEmpty() ? parsed->profile
                                         : parsed->snapshot;
        statEntry(directory_entry(name));
        return KIO::WorkerResult::pass();
    }
    Session* active = session(parsed->profile);
    if (active == nullptr)
        return session_failure();
    const BrowseSessionPin pin(active->id);
    if (!pin)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const auto relative = resolve_entry(*parsed, *active);
    if (!relative)
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    const auto entry = remote_entry(active->id, QString::fromStdString(relative->string()));
    if (!entry)
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    KIO::UDSEntry result;
    result.fastInsert(KIO::UDSEntry::UDS_NAME, relative->filename().empty() ? u"."_s : QString::fromStdString(relative->filename().string()));
    result.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, entry->directory ? S_IFDIR : S_IFREG);
    result.fastInsert(KIO::UDSEntry::UDS_SIZE, static_cast<KIO::filesize_t>(entry->size));
    result.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, entry->modified_at);
    result.fastInsert(KIO::UDSEntry::UDS_ACCESS, static_cast<long long>(entry->mode & 0777));
    result.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, entry_mime_type(*entry));
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
        return session_failure();
    const BrowseSessionPin pin(active->id);
    if (!pin)
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED);
    const auto relative = resolve_entry(*parsed, *active);
    if (!relative)
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    const auto entry = remote_entry(active->id, QString::fromStdString(relative->string()));
    if (!entry)
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST);
    mimeType(entry->directory ? u"inode/directory"_s : QMimeDatabase{}.mimeTypeForFile(QString::fromStdString(relative->filename().string()), QMimeDatabase::MatchExtension).name());
    return KIO::WorkerResult::pass();
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
        (void)btrfsbackup::kde::BrowseSessionClient{}.setActive(session_id, false);
    } catch (...) {}
}

void BtrfsBackupWorker::close_sessions() noexcept {
    for (const Session& session : std::as_const(sessions_)) {
        try {
            (void)btrfsbackup::kde::BrowseSessionClient{}.close(session.id);
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
