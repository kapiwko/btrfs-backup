// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTime>

#include <core/ManagerProtocol.hpp>

class ScreenshotSplash final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KSplash")

  public slots:
    void setStage(const QString&) {
    }
};

class ScreenshotManager final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.btrfsbackup.Manager1")

  public:
    explicit ScreenshotManager(QString mode, QString page)
        : mode_(std::move(mode)), page_(std::move(page)), now_(QDateTime::currentDateTimeUtc()) {
    }

  public slots:
    QString GetCapabilities() const {
        return QStringLiteral(R"({"schemaVersion":1,"interface":"io.github.btrfsbackup.Manager1","apiMajor":2,"apiMinor":7,"profileSchemaVersion":4,"publicStatusSchemaVersion":6,"historySchemaVersion":3,"deviceStateSchemaVersion":1,"readOnly":false,"features":["profiles","status","sanitized-history","device-state","target-storage-usage","start-backup","cancel-backup","validate-target","eject-target","change-signals","browse-backups","profile-administration","profile-details","target-credentials","device-provisioning"]})");
    }

    QString ListProfiles() const {
        return QStringLiteral(R"([{"schemaVersion":2,"profileId":"home","name":"Home backup","enabled":true,"targetName":"Portable Backup","sources":[{"id":"documents","name":"Documents"}],"configurationValid":true,"configurationErrorCode":""},{"schemaVersion":2,"profileId":"archive","name":"Project archive","enabled":false,"targetName":"Studio Archive","sources":[{"id":"projects","name":"Projects"}],"configurationValid":true,"configurationErrorCode":""}])");
    }

    QString GetStatus(const QString& profile) const {
        if (profile == QStringLiteral("home") && mode_ == QStringLiteral("transferring")) {
            const QString started = now_.addSecs(-(4 * 60 * 60 + 14 * 60)).toString(Qt::ISODate);
            const QString previous = now_.addDays(-3).toString(Qt::ISODate);
            return QStringLiteral(R"({"schemaVersion":6,"runId":"run-1","operationKind":"backup","state":"running","phase":"transfer","activity":"transferring","canCancel":true,"errorCode":"","sourceName":"Documents","targetName":"Portable Backup","bytesProcessed":68719476736,"bytesTotalEstimated":118111600640,"speedBps":94371840,"etaSeconds":540,"sourceProgress":58,"overallProgress":58,"progressAccuracy":"exact","sourceIndex":1,"sourceCount":1,"startedAt":"%1","updatedAt":"%2","lastSuccessAt":"%3","lastAttemptAt":"%3","lastAttemptState":"succeeded"})")
                .arg(started, now_.toString(Qt::ISODate), previous);
        }
        if (profile == QStringLiteral("home")) {
            QDateTime previous = now_.addDays(-3);
            if (page_ == QStringLiteral("profile-details")) {
                previous = now_.addDays(-4);
                previous.setTime(QTime(21, 14));
            }
            const QString completed = previous.toString(Qt::ISODate);
            return QStringLiteral(R"({"schemaVersion":6,"runId":"run-0","operationKind":"backup","state":"succeeded","phase":"completed","activity":"idle","canCancel":false,"errorCode":"","sourceName":"Documents","targetName":"Portable Backup","bytesProcessed":68719476736,"bytesTotalEstimated":68719476736,"speedBps":0,"etaSeconds":-1,"sourceProgress":100,"overallProgress":100,"progressAccuracy":"exact","sourceIndex":1,"sourceCount":1,"startedAt":"%1","updatedAt":"%2","lastSuccessAt":"%2","lastAttemptAt":"%2","lastAttemptState":"succeeded"})")
                .arg(previous.addSecs(-24 * 60).toString(Qt::ISODate), completed);
        }
        return QStringLiteral(R"({"schemaVersion":6,"runId":"","operationKind":"backup","state":"idle","phase":"idle","activity":"idle","canCancel":false,"errorCode":"","sourceName":"","targetName":"Studio Archive","bytesProcessed":0,"bytesTotalEstimated":0,"speedBps":0,"etaSeconds":-1,"sourceProgress":-1,"overallProgress":-1,"progressAccuracy":"unknown","sourceIndex":0,"sourceCount":1,"startedAt":"","updatedAt":"","lastSuccessAt":"2026-08-27T18:42:00Z","lastAttemptAt":"2026-08-27T18:42:00Z","lastAttemptState":"succeeded"})");
    }

    QString GetDeviceState(const QString& profile) const {
        const bool connected = mode_ != QStringLiteral("disconnected") &&
            (profile == QStringLiteral("home") || mode_ == QStringLiteral("connected"));
        const QString name = profile == QStringLiteral("home")
            ? QStringLiteral("Portable Backup")
            : QStringLiteral("Studio Archive");
        if (!connected) {
            return QStringLiteral(R"({"schemaVersion":1,"profileId":"%1","targetName":"%2","state":"disconnected","connected":false,"unlocked":false,"mounted":false,"safeToRemove":false})")
                .arg(profile, name);
        }
        return QStringLiteral(R"({"schemaVersion":1,"profileId":"%1","targetName":"%2","state":"mounted","connected":true,"unlocked":true,"mounted":true,"safeToRemove":true,"storage":{"schemaVersion":1,"capacityBytes":3958241859994,"usedBytes":1319413953331,"availableBytes":2638827906663,"usagePercent":33,"measuredAt":"2026-09-03T08:12:00Z","live":true,"spaceState":"normal"}})")
            .arg(profile, name);
    }

    QString GetHistorySanitized(const QString& profile, uint, uint) const {
        if (profile != QStringLiteral("home") || mode_ == QStringLiteral("transferring"))
            return QStringLiteral("[]");
        return QStringLiteral(R"([{"schemaVersion":3,"state":"succeeded","errorCode":"","sourceName":"Documents","targetName":"Portable Backup","startedAt":"2026-09-02T21:08:54Z","finishedAt":"2026-09-02T21:32:54Z","sourceCount":2,"overallProgress":100,"bytesTransferred":68719476736},{"schemaVersion":3,"state":"succeeded","errorCode":"","sourceName":"Projects","targetName":"Portable Backup","startedAt":"2026-09-01T18:14:00Z","finishedAt":"2026-09-01T18:23:42Z","sourceCount":2,"overallProgress":100,"bytesTransferred":12884901888},{"schemaVersion":3,"state":"cancelled","errorCode":"operation.cancelled","sourceName":"Documents","targetName":"Portable Backup","startedAt":"2026-08-31T07:40:00Z","finishedAt":"2026-08-31T07:43:18Z","sourceCount":2,"overallProgress":37,"bytesTransferred":4294967296},{"schemaVersion":3,"state":"failed","errorCode":"target.unavailable","sourceName":"Documents","targetName":"Portable Backup","startedAt":"2026-08-30T20:50:00Z","finishedAt":"2026-08-30T20:50:08Z","sourceCount":2,"overallProgress":4,"bytesTransferred":0}])");
    }

    QString GetProfileDetails(const QString&) const {
        return QStringLiteral(R"({"schemaVersion":2,"profileId":"home","generation":"0123456789abcdef0123456789abcdef","fingerprint":"abcdef0123456789abcdef0123456789","configurationValid":true,"configurationErrorCode":"","sourceCandidates":["/home","/srv/projects"],"document":{"schemaVersion":4,"profileId":"home","name":"Home backup","enabled":true,"target":{"device":"/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555","luksUuid":"11111111-2222-3333-4444-555555555555","btrfsUuid":"66666666-7777-8888-9999-aaaaaaaaaaaa","partitionUuid":"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee","serial":"Portable-Backup-001","mapperName":"backupdisk","activation":{"mode":"askPassword"}},"paths":{"remoteRoot":"/mnt/btrfs-backup/home/snapshots","incomingRoot":"/mnt/btrfs-backup/home/.incoming"},"settings":{"dailyLimit":false,"incrementalRequired":true,"keepFailedLocalSnapshot":false,"autoEject":true,"remoteRetention":30,"localRetention":30,"minimumTargetFreeBytes":5368709120,"minimumLocalFreeBytes":1073741824},"sources":[{"id":"documents","name":"Documents","enabled":true,"subvolume":"/home","localSnapshotDir":"/.snapshots/home","remoteSubdir":"home","remoteRetention":30,"localRetention":30}]}})");
    }

    QString ListTargetCredentials(const QString&) const {
        return QStringLiteral(R"([{"schemaVersion":1,"id":"slot-0","label":"Recovery passphrase","type":"passphrase","keyslot":0,"managed":false,"automatic":false},{"schemaVersion":1,"id":"slot-1","label":"Automatic backups","type":"keyFile","keyslot":1,"managed":true,"automatic":true}])");
    }

    QString InspectStorageTopology() const {
        return QStringLiteral(
            R"({"schemaVersion":%1,"generation":"screenshot-topology-1","devices":[{"candidateId":"device-portable-backup","displayIndex":1,"transport":"usb","sizeBytes":2000398934016,"logicalSectorSize":512,"physicalSectorSize":4096,"removable":true,"hotplug":true,"systemDevice":false,"mounted":false,"containsData":true,"readOnly":false,"partitionTableType":"gpt","regions":[{"candidateId":"partition-portable-backup","kind":"existing-partition","partitionNumber":1,"encrypted":true,"mounted":false,"startSector":2048,"sectorCount":2734375000,"configuredBackupTarget":false,"suitableForReformat":true,"suitableForAdoption":true,"blockers":[]},{"candidateId":"partition-archive-backup","kind":"existing-partition","partitionNumber":2,"encrypted":true,"mounted":false,"startSector":2734377048,"sectorCount":1172652120,"configuredBackupTarget":false,"suitableForReformat":true,"suitableForAdoption":true,"blockers":[]}],"blockers":[]},{"candidateId":"device-sandisk-extreme","displayIndex":2,"transport":"usb","sizeBytes":1000204886016,"logicalSectorSize":512,"physicalSectorSize":4096,"removable":true,"hotplug":true,"systemDevice":false,"mounted":false,"containsData":true,"readOnly":false,"partitionTableType":"gpt","regions":[{"candidateId":"partition-sandisk-data","kind":"existing-partition","partitionNumber":1,"encrypted":false,"mounted":false,"startSector":2048,"sectorCount":1269790000,"configuredBackupTarget":false,"suitableForReformat":true,"suitableForAdoption":false,"blockers":[]},{"candidateId":"free-sandisk","kind":"unallocated","startSector":1269792048,"sectorCount":683731000,"suitableForBackupPartition":true,"blockers":[]}],"blockers":[]}]})"
        )
            .arg(btrfsbackup::manager_protocol::storage_topology_schema_version);
    }

    QString ListSourceCandidates() const {
        return QStringLiteral(R"([{"id":"source-root","path":"/","filesystemUuid":"00000000-aaaa-bbbb-cccc-000000000000","mountRoot":"/","localSnapshotRoot":"/.snapshots/btrfs-backup"},{"id":"source-home","path":"/home","filesystemUuid":"11111111-aaaa-bbbb-cccc-111111111111","mountRoot":"/home","localSnapshotRoot":"/home/.snapshots/btrfs-backup"},{"id":"source-projects","path":"/srv/projects","filesystemUuid":"22222222-aaaa-bbbb-cccc-222222222222","mountRoot":"/srv/projects","localSnapshotRoot":"/srv/projects/.snapshots/btrfs-backup"}])");
    }

    QString OpenBrowseSession(const QString& profile) const {
        return browseSession(profile);
    }

    QString RenewBrowseSession(const QString&) const {
        return browseSession(QStringLiteral("home"));
    }

    QString CloseBrowseSession(const QString&) const {
        return QStringLiteral(R"({"accepted":true})");
    }

    QString ListBrowseDirectory(const QString&, const QString& path) const {
        if (path == QStringLiteral(".")) {
            return QStringLiteral(
                R"({"schemaVersion":1,"entries":[{"name":"home-2026-09-01T181400Z","kind":"directory","size":0,"mode":365,"modifiedAt":1788286440},{"name":"home-2026-09-02T230854Z","kind":"directory","size":0,"mode":365,"modifiedAt":1788390534}]})"
            );
        }
        return QStringLiteral(
            R"({"schemaVersion":1,"entries":[{"name":"Photos","kind":"directory","size":0,"mode":365,"modifiedAt":1788389820},{"name":"Projects","kind":"directory","size":0,"mode":365,"modifiedAt":1788389400},{"name":"Reports","kind":"directory","size":0,"mode":365,"modifiedAt":1788389100},{"name":"Backup inventory.pdf","kind":"file","size":1847296,"mode":292,"modifiedAt":1788388920},{"name":"Budget 2026.ods","kind":"file","size":286720,"mode":292,"modifiedAt":1788388500},{"name":"report.odt","kind":"file","size":2936012,"mode":292,"modifiedAt":1788388200}]})"
        );
    }

    QString ListBrowseDirectoryPage(const QString&, const QString& path, const QString&, uint) const {
        const QString entries = path.endsWith(QStringLiteral("Documents"))
            ? QStringLiteral(
                  R"([{"name":"Photos","kind":"directory","size":0,"mode":365,"modifiedAt":1788389820},{"name":"Projects","kind":"directory","size":0,"mode":365,"modifiedAt":1788389400},{"name":"Reports","kind":"directory","size":0,"mode":365,"modifiedAt":1788389100},{"name":"Backup inventory.pdf","kind":"file","size":1847296,"mode":292,"modifiedAt":1788388920},{"name":"Budget 2026.ods","kind":"file","size":286720,"mode":292,"modifiedAt":1788388500},{"name":"report.odt","kind":"file","size":2936012,"mode":292,"modifiedAt":1788388200}])"
              )
            : QStringLiteral("[]");
        return QStringLiteral(R"({"schemaVersion":1,"entries":%1,"continuationToken":""})").arg(entries);
    }

    QString InspectBrowseRepository(const QString&) const {
        return QStringLiteral(
            R"({"schemaVersion":1,"snapshots":[{"snapshotId":"home-2026-09-01T181400Z","profileId":"home","sourceId":"documents","relativePath":"snapshots/documents/home-2026-09-01T181400Z","createdAt":"2026-09-01T18:14:00Z","verified":true},{"snapshotId":"home-2026-09-02T230854Z","profileId":"home","sourceId":"documents","relativePath":"snapshots/documents/home-2026-09-02T230854Z","createdAt":"2026-09-02T23:08:54Z","verified":true}]})"
        );
    }

    QString InspectBrowseEntry(const QString&, const QString& path) const {
        const QString name = path.section(u'/', -1);
        const bool directory = !name.contains(u'.');
        return QStringLiteral(
                   R"({"schemaVersion":1,"name":"%1","kind":"%2","size":%3,"mode":%4,"modifiedAt":1788390534})"
        )
            .arg(
                name,
                directory ? QStringLiteral("directory") : QStringLiteral("file"),
                directory ? QStringLiteral("0") : QStringLiteral("2936012"),
                directory ? QStringLiteral("365") : QStringLiteral("292")
            );
    }

    QString BuildDevicePreparationPlan(const QString& request) const {
        const QJsonObject selection = QJsonDocument::fromJson(request.toUtf8()).object();
        const QString candidate = selection.value(QStringLiteral("candidateId")).toString();
        const QString mode = selection.value(QStringLiteral("mode")).toString();
        QString identity;
        if (mode == QStringLiteral("reformat-existing-partition"))
            identity = QStringLiteral(R"(,"deviceId":"device-sandisk-extreme","partitionId":"%1")").arg(candidate);
        else if (mode == QStringLiteral("create-partition-in-unallocated-space"))
            identity = QStringLiteral(R"(,"deviceId":"device-sandisk-extreme","freeRegionId":"%1")").arg(candidate);
        else
            identity = QStringLiteral(R"(,"deviceId":"%1")").arg(candidate);
        const QString before = QStringLiteral(
                                   R"({"deviceId":"device-sandisk-extreme","sizeBytes":1000204886016,"logicalSectorSize":512,"partitionTableType":"gpt","regions":[{"candidateId":"partition-sandisk-data","kind":"existing-partition","startSector":2048,"sectorCount":1269790000,"partitionNumber":1,"path":"/dev/sdc1","partitionLabel":"Files","filesystemType":"ext4","geometryExact":true,"encrypted":false,"changed":false,"dataWillBeErased":%1},{"candidateId":"free-sandisk","kind":"unallocated","startSector":1269792048,"sectorCount":683731000,"partitionNumber":0,"path":"","partitionLabel":"","filesystemType":"","geometryExact":true,"encrypted":false,"changed":false,"dataWillBeErased":false}]})"
        )
                                   .arg(mode == QStringLiteral("reformat-existing-partition")
                                            ? QStringLiteral("true")
                                            : QStringLiteral("false"));
        const QString after = mode == QStringLiteral("create-partition-in-unallocated-space")
            ? QStringLiteral(
                  R"({"deviceId":"device-sandisk-extreme","sizeBytes":1000204886016,"logicalSectorSize":512,"partitionTableType":"gpt","regions":[{"candidateId":"partition-sandisk-data","kind":"existing-partition","startSector":2048,"sectorCount":1269790000,"partitionNumber":1,"path":"/dev/sdc1","partitionLabel":"Files","filesystemType":"ext4","geometryExact":true,"encrypted":false,"changed":false,"dataWillBeErased":false},{"candidateId":"planned-backup-partition","kind":"backup-partition","startSector":1269792048,"sectorCount":683731000,"partitionNumber":2,"path":"/dev/sdc2","partitionLabel":"Btrfs Backup","filesystemType":"btrfs","geometryExact":true,"encrypted":true,"changed":true,"dataWillBeErased":false}]})"
              )
            : QStringLiteral(
                  R"({"deviceId":"device-sandisk-extreme","sizeBytes":1000204886016,"logicalSectorSize":512,"partitionTableType":"gpt","regions":[{"candidateId":"partition-sandisk-data","kind":"backup-partition","startSector":2048,"sectorCount":1269790000,"partitionNumber":1,"path":"/dev/sdc1","partitionLabel":"Btrfs Backup","filesystemType":"btrfs","geometryExact":true,"encrypted":true,"changed":true,"dataWillBeErased":false},{"candidateId":"free-sandisk","kind":"unallocated","startSector":1269792048,"sectorCount":683731000,"partitionNumber":0,"path":"","partitionLabel":"","filesystemType":"","geometryExact":true,"encrypted":false,"changed":false,"dataWillBeErased":false}]})"
              );
        return QStringLiteral(
                   R"({"schemaVersion":%1,"planId":"screenshot-plan-1","topologyGeneration":"screenshot-topology-1","mode":"%2"%3,"before":%4,"after":%5,"operations":["erase-partition-signatures","format-luks2","format-btrfs"],"warnings":[],"destructiveScope":"existing-partition"})"
        )
            .arg(btrfsbackup::manager_protocol::device_preparation_plan_schema_version)
            .arg(mode, identity, before, after);
    }

  signals:
    void ProfilesChanged();
    void StatusChanged(const QString& profile);
    void HistoryChanged(const QString& profile);
    void DeviceStateChanged(const QString& profile);

  private:
    static QString browseSession(const QString& profile) {
        return QStringLiteral(
                   R"({"schemaVersion":%1,"sessionId":"readme-browse-session","profileId":"%2","expiresAt":"2026-09-03T13:00:00Z","readOnly":true})"
        )
            .arg(btrfsbackup::manager_protocol::browse_session_schema_version)
            .arg(profile);
    }

    QString mode_;
    QString page_;
    QDateTime now_;
};

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    ScreenshotManager manager(
        application.arguments().value(1, QStringLiteral("connected")),
        application.arguments().value(2)
    );
    ScreenshotSplash splash;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(
            QStringLiteral("/io/github/btrfsbackup/Manager1"),
            &manager,
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals
        ) ||
        !bus.registerService(QStringLiteral("io.github.btrfsbackup.Manager1")) ||
        !bus.registerObject(QStringLiteral("/KSplash"), &splash, QDBusConnection::ExportAllSlots) ||
        !bus.registerService(QStringLiteral("org.kde.KSplash"))) {
        return 2;
    }
    return application.exec();
}

#include "kde-manager-demo.moc"
