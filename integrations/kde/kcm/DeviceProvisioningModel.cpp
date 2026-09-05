// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeviceProvisioningModel.hpp"

#include "ManagerErrorMessage.hpp"

#include <ManagerApi.hpp>
#include <core/ManagerProtocol.hpp>

#include <KLocalizedString>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>

namespace btrfsbackup::kde::kcm {

namespace {
QDBusUnixFileDescriptor secret_descriptor(const QString& value) {
    QByteArray bytes = value.toUtf8();
    if (bytes.isEmpty() || bytes.size() > 4096)
        return {};
    const int fd = ::memfd_create("btrfs-backup-device-secret", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0)
        return {};
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::write(fd, bytes.constData() + offset, static_cast<std::size_t>(bytes.size() - offset));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            bytes.fill('\0');
            ::close(fd);
            return {};
        }
        offset += count;
    }
    bytes.fill('\0');
    if (::fcntl(fd, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) < 0) {
        ::close(fd);
        return {};
    }
    (void)::lseek(fd, 0, SEEK_SET);
    QDBusUnixFileDescriptor result(fd);
    ::close(fd);
    return result;
}
} // namespace

DeviceProvisioningModel::DeviceProvisioningModel(QObject* parent)
    : QObject(parent), bus_(QDBusConnection::systemBus()) {
}
QVariantList DeviceProvisioningModel::devices() const {
    return devices_;
}
QVariantMap DeviceProvisioningModel::topology() const {
    return topology_;
}
QVariantMap DeviceProvisioningModel::plan() const {
    return plan_;
}
QVariantMap DeviceProvisioningModel::inspection() const {
    return inspection_;
}
QVariantMap DeviceProvisioningModel::operation() const {
    return operation_;
}
QVariantList DeviceProvisioningModel::sourceCandidates() const {
    return source_candidates_;
}
bool DeviceProvisioningModel::busy() const {
    return busy_;
}
QString DeviceProvisioningModel::errorMessage() const {
    return error_message_;
}

void DeviceProvisioningModel::refresh() {
    if (!busy_)
        request(RequestKind::Topology, QLatin1String(manager_protocol::method::inspect_storage_topology));
}

void DeviceProvisioningModel::buildPlan(const QVariantMap& selection, const QString& mode) {
    if (busy_ || topology_.isEmpty())
        return;
    const QString path = selection.value(QStringLiteral("path")).toString();
    const QString topology_candidate = selection.value(QStringLiteral("candidateId")).toString();
    if (topology_candidate.isEmpty()) {
        setError(i18nd("kcm_btrfsbackup", "Refresh the storage layout and select the disk again."));
        return;
    }
    plan_.clear();
    pending_plan_path_ = path;
    emit planChanged();
    const QJsonObject payload{
        {QStringLiteral("topologyGeneration"), topology_.value(QStringLiteral("generation")).toString()},
        {QStringLiteral("candidateId"), topology_candidate},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("inspectionId"), mode == QStringLiteral("adopt-existing-target") ? inspection_.value(QStringLiteral("inspectionId")).toString() : QString{}},
    };
    request(
        RequestKind::Plan,
        QLatin1String(manager_protocol::method::build_device_preparation_plan),
        {QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}
    );
}

void DeviceProvisioningModel::inspectExistingTarget(
    const QVariantMap& selection,
    const QString& passphrase
) {
    if (busy_ || topology_.isEmpty())
        return;
    const QString candidate = selection.value(QStringLiteral("candidateId")).toString();
    if (candidate.isEmpty()) {
        setError(i18nd("kcm_btrfsbackup", "Refresh the storage layout and select the partition again."));
        return;
    }
    const auto secret = secret_descriptor(passphrase);
    if (!secret.isValid()) {
        setError(i18nd("kcm_btrfsbackup", "A passphrase must contain between 1 and 4096 bytes."));
        return;
    }
    clearSelection();
    pending_inspection_selection_ = selection;
    const QJsonObject payload{
        {QStringLiteral("topologyGeneration"), topology_.value(QStringLiteral("generation")).toString()},
        {QStringLiteral("candidateId"), candidate},
    };
    request(
        RequestKind::Inspection,
        QLatin1String(manager_protocol::method::inspect_existing_target),
        {QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)), QVariant::fromValue(secret)}
    );
}

void DeviceProvisioningModel::clearSelection() {
    plan_.clear();
    inspection_.clear();
    pending_plan_path_.clear();
    pending_inspection_selection_.clear();
    emit planChanged();
    emit inspectionChanged();
}

void DeviceProvisioningModel::start(
    const QString& profile_id,
    const QString& profile_name,
    const QString& source_candidate_id,
    const QString& passphrase,
    const QString& confirmation,
    bool automatic_key
) {
    if (busy_)
        return;
    if (passphrase != confirmation) {
        setError(i18nd("kcm_btrfsbackup", "The passphrases do not match."));
        return;
    }
    const QString plan_id = plan_.value(QStringLiteral("planId")).toString();
    if (plan_id.isEmpty()) {
        setError(i18nd("kcm_btrfsbackup", "Refresh the storage layout and review the preparation plan again."));
        return;
    }
    const auto secret = secret_descriptor(passphrase);
    if (!secret.isValid()) {
        setError(i18nd("kcm_btrfsbackup", "A passphrase must contain between 1 and 4096 bytes."));
        return;
    }
    const QJsonObject payload{
        {QStringLiteral("profileId"), profile_id.trimmed()},
        {QStringLiteral("profileName"), profile_name.trimmed()},
        {QStringLiteral("planId"), plan_id},
        {QStringLiteral("sourceCandidateId"), source_candidate_id.trimmed()},
        {QStringLiteral("passphraseLabel"), i18nd("kcm_btrfsbackup", "Recovery passphrase")},
        {QStringLiteral("createAutomaticKey"), automatic_key},
    };
    request(
        RequestKind::Start,
        QLatin1String(manager_protocol::method::start_device_preparation),
        {QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)), QVariant::fromValue(secret)}
    );
}

void DeviceProvisioningModel::poll() {
    const QString id = operation_.value(QStringLiteral("operationId")).toString();
    if (!busy_ && !id.isEmpty())
        request(RequestKind::Poll, QLatin1String(manager_protocol::method::get_device_preparation), {id});
}

void DeviceProvisioningModel::cancel() {
    const QString id = operation_.value(QStringLiteral("operationId")).toString();
    if (!busy_ && !id.isEmpty() && operation_.value(QStringLiteral("canCancel")).toBool())
        request(RequestKind::Cancel, QLatin1String(manager_protocol::method::cancel_device_preparation), {id});
}

void DeviceProvisioningModel::clearError() {
    setError({});
}

void DeviceProvisioningModel::request(RequestKind kind, const QString& method, const QVariantList& arguments) {
    busy_ = true;
    setError({});
    emit stateChanged();
    auto* watcher = new QDBusPendingCallWatcher(manager_call(bus_, method, arguments), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, kind](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        busy_ = false;
        if (reply.isError()) {
            setError(manager_error_message(reply.error()));
            emit stateChanged();
            return;
        }
        bool valid = true;
        if (kind == RequestKind::Topology)
            valid = applyTopology(reply.value());
        else if (kind == RequestKind::Sources)
            valid = applySources(reply.value());
        else if (kind == RequestKind::Inspection)
            valid = applyInspection(reply.value());
        else if (kind == RequestKind::Plan)
            valid = applyPlan(reply.value());
        else if (kind == RequestKind::Start || kind == RequestKind::Poll)
            valid = applyOperation(reply.value());
        if (!valid)
            setError(i18nd("kcm_btrfsbackup", "The backup manager returned an invalid device preparation response."));
        emit stateChanged();
        if (valid && kind == RequestKind::Topology)
            request(RequestKind::Sources, QLatin1String(manager_protocol::method::list_source_candidates));
    });
}

bool DeviceProvisioningModel::applyTopology(const QString& payload) {
    const auto document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject())
        return false;
    const auto object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) !=
            manager_protocol::storage_topology_schema_version ||
        object.value(QStringLiteral("generation")).toString().isEmpty() ||
        !object.value(QStringLiteral("devices")).isArray())
        return false;
    topology_ = object.toVariantMap();
    devices_ = object.value(QStringLiteral("devices")).toArray().toVariantList();
    plan_.clear();
    inspection_.clear();
    pending_plan_path_.clear();
    pending_inspection_selection_.clear();
    emit topologyChanged();
    emit devicesChanged();
    emit planChanged();
    emit inspectionChanged();
    return true;
}

bool DeviceProvisioningModel::applyInspection(const QString& payload) {
    const auto document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject())
        return false;
    const auto object = document.object();
    const QString classification = object.value(QStringLiteral("classification")).toString();
    const bool compatible = classification == QStringLiteral("compatible-repository");
    const bool known_classification = compatible || classification == QStringLiteral("empty-filesystem") ||
        classification == QStringLiteral("legacy-repository") ||
        classification == QStringLiteral("unsupported-repository") ||
        classification == QStringLiteral("foreign-or-invalid-repository") ||
        classification == QStringLiteral("not-btrfs-filesystem");
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) !=
            manager_protocol::existing_target_inspection_schema_version ||
        !known_classification ||
        object.value(QStringLiteral("luksUuid")).toString().isEmpty() ||
        object.value(QStringLiteral("partitionUuid")).toString().isEmpty() ||
        !object.value(QStringLiteral("catalogGeneration")).isDouble() ||
        !object.value(QStringLiteral("snapshotCount")).isDouble() ||
        (compatible && (object.value(QStringLiteral("inspectionId")).toString().isEmpty() || object.value(QStringLiteral("repositoryId")).toString().isEmpty() || object.value(QStringLiteral("btrfsUuid")).toString().isEmpty())))
        return false;
    const QString candidate = pending_inspection_selection_.value(QStringLiteral("candidateId")).toString();
    if (object.value(QStringLiteral("topologyGeneration")).toString() !=
            topology_.value(QStringLiteral("generation")).toString() ||
        object.value(QStringLiteral("partitionId")).toString() != candidate)
        return false;
    inspection_ = object.toVariantMap();
    plan_.clear();
    emit inspectionChanged();
    emit planChanged();
    const QVariantMap selection = pending_inspection_selection_;
    pending_inspection_selection_.clear();
    if (compatible)
        buildPlan(selection, QStringLiteral("adopt-existing-target"));
    return true;
}

bool DeviceProvisioningModel::applyPlan(const QString& payload) {
    const auto document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject())
        return false;
    const auto object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) !=
            manager_protocol::device_preparation_plan_schema_version ||
        object.value(QStringLiteral("planId")).toString().isEmpty() ||
        !object.value(QStringLiteral("before")).isObject() || !object.value(QStringLiteral("after")).isObject())
        return false;
    plan_ = object.toVariantMap();
    plan_.insert(QStringLiteral("displayPath"), pending_plan_path_);
    emit planChanged();
    return true;
}

bool DeviceProvisioningModel::applySources(const QString& payload) {
    const auto document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isArray())
        return false;
    QVariantList result;
    for (const auto& value : document.array()) {
        if (!value.isObject())
            return false;
        const auto candidate = value.toObject();
        if (candidate.value(QStringLiteral("id")).toString().isEmpty() ||
            candidate.value(QStringLiteral("path")).toString().isEmpty() ||
            candidate.value(QStringLiteral("filesystemUuid")).toString().isEmpty() ||
            candidate.value(QStringLiteral("mountRoot")).toString().isEmpty() ||
            candidate.value(QStringLiteral("localSnapshotRoot")).toString().isEmpty())
            return false;
        QVariantMap displayed = candidate.toVariantMap();
        const QString path = candidate.value(QStringLiteral("path")).toString();
        const QString name = path == QStringLiteral("/home")
            ? i18nd("kcm_btrfsbackup", "Home folder")
            : path == QStringLiteral("/")
            ? i18nd("kcm_btrfsbackup", "System root")
            : i18nd("kcm_btrfsbackup", "Btrfs source");
        displayed.insert(QStringLiteral("displayName"), i18nd("kcm_btrfsbackup", "%1 — %2", name, path));
        result.push_back(std::move(displayed));
    }
    source_candidates_ = std::move(result);
    emit sourceCandidatesChanged();
    return true;
}

bool DeviceProvisioningModel::applyOperation(const QString& payload) {
    const auto document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject())
        return false;
    const auto object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != manager_protocol::device_provisioning_schema_version ||
        object.value(QStringLiteral("operationId")).toString().isEmpty() ||
        !object.value(QStringLiteral("lastCompletedPhase")).isString() ||
        !object.value(QStringLiteral("cleanupResult")).isString())
        return false;
    operation_ = object.toVariantMap();
    emit operationChanged();
    if (object.value(QStringLiteral("state")).toString() == QStringLiteral("succeeded"))
        emit completed(object.value(QStringLiteral("profileId")).toString());
    return true;
}

void DeviceProvisioningModel::setError(const QString& message) {
    if (error_message_ == message)
        return;
    error_message_ = message;
    emit stateChanged();
}

} // namespace btrfsbackup::kde::kcm
