#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

class BackupStatusModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString command READ command WRITE setCommand NOTIFY commandChanged)
    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY profileChanged)
    Q_PROPERTY(QString profileName READ profileName NOTIFY statusChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY statusChanged)
    Q_PROPERTY(QString message READ message NOTIFY statusChanged)
    Q_PROPERTY(QString currentSourceId READ currentSourceId NOTIFY statusChanged)
    Q_PROPERTY(QString currentSourceName READ currentSourceName NOTIFY statusChanged)
    Q_PROPERTY(int sourceIndex READ sourceIndex NOTIFY statusChanged)
    Q_PROPERTY(int sourceCount READ sourceCount NOTIFY statusChanged)
    Q_PROPERTY(qint64 bytesProcessed READ bytesProcessed NOTIFY statusChanged)
    Q_PROPERTY(qint64 bytesTotalEstimated READ bytesTotalEstimated NOTIFY statusChanged)
    Q_PROPERTY(qint64 runBytesProcessed READ runBytesProcessed NOTIFY statusChanged)
    Q_PROPERTY(qint64 speedBps READ speedBps NOTIFY statusChanged)
    Q_PROPERTY(int etaSeconds READ etaSeconds NOTIFY statusChanged)
    Q_PROPERTY(int sourceProgress READ sourceProgress NOTIFY statusChanged)
    Q_PROPERTY(int overallProgress READ overallProgress NOTIFY statusChanged)
    Q_PROPERTY(QString progressAccuracy READ progressAccuracy NOTIFY statusChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY statusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY statusChanged)
    Q_PROPERTY(QString suggestedAction READ suggestedAction NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)

public:
    explicit BackupStatusModel(QObject* parent = nullptr);
    ~BackupStatusModel() override;

    QString command() const;
    void setCommand(const QString& command);

    QString profile() const;
    void setProfile(const QString& profile);

    bool connected() const;
    QString profileName() const;
    QString state() const;
    QString phase() const;
    QString message() const;
    QString currentSourceId() const;
    QString currentSourceName() const;
    int sourceIndex() const;
    int sourceCount() const;
    qint64 bytesProcessed() const;
    qint64 bytesTotalEstimated() const;
    qint64 runBytesProcessed() const;
    qint64 speedBps() const;
    int etaSeconds() const;
    int sourceProgress() const;
    int overallProgress() const;
    QString progressAccuracy() const;
    QString errorCode() const;
    QString errorMessage() const;
    QString suggestedAction() const;
    QString lastError() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void commandChanged();
    void profileChanged();
    void connectedChanged();
    void statusChanged();
    void errorChanged();

private:
    void readWatchOutput();
    void processWatchBuffer();
    bool takeJsonObject(QByteArray& object);
    void applyStatusObject(const QByteArray& object);
    void setConnected(bool connected);
    void setLastError(const QString& message);

    QString command_ = QStringLiteral("btrfs-backupctl");
    QString profile_ = QStringLiteral("default");
    QProcess watch_;
    QByteArray watch_buffer_;
    bool connected_ = false;
    QString profile_name_;
    QString state_ = QStringLiteral("unknown");
    QString phase_;
    QString message_;
    QString current_source_id_;
    QString current_source_name_;
    int source_index_ = 0;
    int source_count_ = 0;
    qint64 bytes_processed_ = 0;
    qint64 bytes_total_estimated_ = 0;
    qint64 run_bytes_processed_ = 0;
    qint64 speed_bps_ = 0;
    int eta_seconds_ = -1;
    int source_progress_ = -1;
    int overall_progress_ = -1;
    QString progress_accuracy_ = QStringLiteral("indeterminate");
    QString error_code_;
    QString error_message_;
    QString suggested_action_;
    QString last_error_;
};
