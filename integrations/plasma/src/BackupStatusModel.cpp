#include "BackupStatusModel.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

#include <cctype>

namespace {

qint64 json_int64(const QJsonObject& object, const char* key, qint64 fallback = 0)
{
    const auto value = object.value(QLatin1String(key));
    if (value.isDouble()) {
        return static_cast<qint64>(value.toDouble());
    }
    return fallback;
}

int json_int(const QJsonObject& object, const char* key, int fallback = 0)
{
    const auto value = object.value(QLatin1String(key));
    if (value.isDouble()) {
        return value.toInt();
    }
    return fallback;
}

QString json_string(const QJsonObject& object, const char* key)
{
    return object.value(QLatin1String(key)).toString();
}

} // namespace

BackupStatusModel::BackupStatusModel(QObject* parent)
    : QObject(parent)
{
    connect(&watch_, &QProcess::readyReadStandardOutput, this, &BackupStatusModel::readWatchOutput);
    connect(&watch_, &QProcess::readyReadStandardError, this, [this]() {
        const QString output = QString::fromUtf8(watch_.readAllStandardError()).trimmed();
        if (!output.isEmpty()) {
            setLastError(output);
        }
    });
    connect(&watch_, &QProcess::stateChanged, this, [this](QProcess::ProcessState state) {
        setConnected(state != QProcess::NotRunning);
    });
    connect(&watch_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        setLastError(watch_.errorString());
        setConnected(false);
    });
    connect(&watch_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exit_code, QProcess::ExitStatus exit_status) {
                setConnected(false);
                if (exit_status != QProcess::NormalExit || exit_code != 0) {
                    setLastError(tr("Status watcher exited with code %1.").arg(exit_code));
                }
            });
}

BackupStatusModel::~BackupStatusModel()
{
    stop();
}

QString BackupStatusModel::command() const
{
    return command_;
}

void BackupStatusModel::setCommand(const QString& command)
{
    if (command_ == command) {
        return;
    }

    const bool restart = watch_.state() != QProcess::NotRunning;
    command_ = command;
    emit commandChanged();
    if (restart) {
        start();
    }
}

QString BackupStatusModel::profile() const
{
    return profile_;
}

void BackupStatusModel::setProfile(const QString& profile)
{
    if (profile_ == profile) {
        return;
    }

    const bool restart = watch_.state() != QProcess::NotRunning;
    profile_ = profile;
    emit profileChanged();
    if (restart) {
        start();
    }
}

bool BackupStatusModel::connected() const
{
    return connected_;
}

QString BackupStatusModel::profileName() const
{
    return profile_name_;
}

QString BackupStatusModel::state() const
{
    return state_;
}

QString BackupStatusModel::phase() const
{
    return phase_;
}

QString BackupStatusModel::message() const
{
    return message_;
}

QString BackupStatusModel::currentSourceId() const
{
    return current_source_id_;
}

QString BackupStatusModel::currentSourceName() const
{
    return current_source_name_;
}

int BackupStatusModel::sourceIndex() const
{
    return source_index_;
}

int BackupStatusModel::sourceCount() const
{
    return source_count_;
}

qint64 BackupStatusModel::bytesProcessed() const
{
    return bytes_processed_;
}

qint64 BackupStatusModel::bytesTotalEstimated() const
{
    return bytes_total_estimated_;
}

qint64 BackupStatusModel::runBytesProcessed() const
{
    return run_bytes_processed_;
}

qint64 BackupStatusModel::speedBps() const
{
    return speed_bps_;
}

int BackupStatusModel::etaSeconds() const
{
    return eta_seconds_;
}

int BackupStatusModel::sourceProgress() const
{
    return source_progress_;
}

int BackupStatusModel::overallProgress() const
{
    return overall_progress_;
}

QString BackupStatusModel::progressAccuracy() const
{
    return progress_accuracy_;
}

bool BackupStatusModel::canCancel() const
{
    return can_cancel_;
}

bool BackupStatusModel::safeToRemove() const
{
    return safe_to_remove_;
}

QString BackupStatusModel::errorCode() const
{
    return error_code_;
}

QString BackupStatusModel::errorMessage() const
{
    return error_message_;
}

QString BackupStatusModel::suggestedAction() const
{
    return suggested_action_;
}

QString BackupStatusModel::lastError() const
{
    return last_error_;
}

void BackupStatusModel::start()
{
    stop();
    watch_buffer_.clear();
    setLastError(QString());

    watch_.setProgram(command_);
    watch_.setArguments(QStringList{
        QStringLiteral("status"),
        QStringLiteral("watch"),
        QStringLiteral("--profile"),
        profile_,
        QStringLiteral("--json"),
        QStringLiteral("--interval"),
        QStringLiteral("1"),
    });
    watch_.start();
}

void BackupStatusModel::stop()
{
    if (watch_.state() == QProcess::NotRunning) {
        setConnected(false);
        return;
    }

    watch_.terminate();
    if (!watch_.waitForFinished(1000)) {
        watch_.kill();
        watch_.waitForFinished(1000);
    }
    setConnected(false);
}

void BackupStatusModel::cancel()
{
    const bool started = QProcess::startDetached(command_, QStringList{
        QStringLiteral("runner"),
        QStringLiteral("cancel"),
        QStringLiteral("--profile"),
        profile_,
    });
    if (!started) {
        setLastError(tr("Could not start cancellation command."));
    }
}

void BackupStatusModel::readWatchOutput()
{
    watch_buffer_.append(watch_.readAllStandardOutput());
    processWatchBuffer();
}

void BackupStatusModel::processWatchBuffer()
{
    QByteArray object;
    while (takeJsonObject(object)) {
        applyStatusObject(object);
        object.clear();
    }
}

bool BackupStatusModel::takeJsonObject(QByteArray& object)
{
    int start = -1;
    for (int i = 0; i < watch_buffer_.size(); ++i) {
        const char c = watch_buffer_.at(i);
        if (c == '{') {
            start = i;
            break;
        }
        if (!std::isspace(static_cast<unsigned char>(c))) {
            watch_buffer_.remove(0, i + 1);
            i = -1;
        }
    }

    if (start < 0) {
        watch_buffer_.clear();
        return false;
    }
    if (start > 0) {
        watch_buffer_.remove(0, start);
    }

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (int i = 0; i < watch_buffer_.size(); ++i) {
        const char c = watch_buffer_.at(i);
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                object = watch_buffer_.left(i + 1);
                watch_buffer_.remove(0, i + 1);
                return true;
            }
        }
    }

    return false;
}

void BackupStatusModel::applyStatusObject(const QByteArray& object)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(object, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        setLastError(tr("Invalid status JSON: %1").arg(error.errorString()));
        return;
    }

    const QJsonObject status = document.object();
    profile_name_ = json_string(status, "profileName");
    state_ = json_string(status, "state");
    if (state_.isEmpty()) {
        state_ = QStringLiteral("unknown");
    }
    phase_ = json_string(status, "phase");
    message_ = json_string(status, "message");
    current_source_id_ = json_string(status, "currentSourceId");
    current_source_name_ = json_string(status, "currentSourceName");
    source_index_ = json_int(status, "sourceIndex");
    source_count_ = json_int(status, "sourceCount");
    bytes_processed_ = json_int64(status, "bytesProcessed");
    bytes_total_estimated_ = json_int64(status, "bytesTotalEstimated");
    run_bytes_processed_ = json_int64(status, "runBytesProcessed");
    speed_bps_ = json_int64(status, "speedBps");
    eta_seconds_ = json_int(status, "etaSeconds", -1);
    source_progress_ = json_int(status, "sourceProgress", -1);
    overall_progress_ = json_int(status, "overallProgress", -1);
    progress_accuracy_ = json_string(status, "progressAccuracy");
    if (progress_accuracy_.isEmpty()) {
        progress_accuracy_ = QStringLiteral("indeterminate");
    }
    can_cancel_ = status.value(QLatin1String("canCancel")).toBool(false);
    safe_to_remove_ = status.value(QLatin1String("safeToRemove")).toBool(false);
    error_code_ = json_string(status, "errorCode");
    error_message_ = json_string(status, "errorMessage");
    suggested_action_ = json_string(status, "suggestedAction");

    setConnected(true);
    emit statusChanged();
}

void BackupStatusModel::setConnected(bool connected)
{
    if (connected_ == connected) {
        return;
    }
    connected_ = connected;
    emit connectedChanged();
}

void BackupStatusModel::setLastError(const QString& message)
{
    if (last_error_ == message) {
        return;
    }
    last_error_ = message;
    emit errorChanged();
}
