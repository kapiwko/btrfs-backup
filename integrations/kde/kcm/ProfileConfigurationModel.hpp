// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QJsonObject>
#include <QObject>
#include <QVariantList>

namespace btrfsbackup::kde::kcm {

class ProfileConfigurationModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString profileId READ profileId NOTIFY draftChanged)
    Q_PROPERTY(QString name READ name NOTIFY draftChanged)
    Q_PROPERTY(bool enabled READ enabled NOTIFY draftChanged)
    Q_PROPERTY(QVariantMap target READ target NOTIFY draftChanged)
    Q_PROPERTY(QVariantMap paths READ paths NOTIFY draftChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY draftChanged)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY draftChanged)
    Q_PROPERTY(int schemaVersion READ schemaVersion NOTIFY draftChanged)
    Q_PROPERTY(QString generation READ generation NOTIFY draftChanged)
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY draftChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY stateChanged)
    Q_PROPERTY(bool newDraft READ newDraft NOTIFY stateChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString operationMessage READ operationMessage NOTIFY stateChanged)
    Q_PROPERTY(QString validationPreview READ validationPreview NOTIFY stateChanged)

  public:
    explicit ProfileConfigurationModel(QObject* parent = nullptr);

    QString profileId() const;
    QString name() const;
    bool enabled() const;
    QVariantMap target() const;
    QVariantMap paths() const;
    QVariantMap settings() const;
    QVariantList sources() const;
    int schemaVersion() const;
    QString generation() const;
    QString fingerprint() const;
    bool loaded() const;
    bool newDraft() const;
    bool dirty() const;
    bool busy() const;
    QString errorCode() const;
    QString errorMessage() const;
    QString operationMessage() const;
    QString validationPreview() const;

    Q_INVOKABLE void load(const QString& profileId);
    Q_INVOKABLE void loadDetails(const QString& profileId);
    Q_INVOKABLE void loadForEditing(const QString& profileId);
    Q_INVOKABLE void createDraft(const QString& profileId);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void setName(const QString& value);
    Q_INVOKABLE void setEnabled(bool value);
    Q_INVOKABLE void setTargetValue(const QString& key, const QVariant& value);
    Q_INVOKABLE void setSettingValue(const QString& key, const QVariant& value);
    Q_INVOKABLE void setSourceValue(int index, const QString& key, const QVariant& value);
    Q_INVOKABLE void addSource();
    Q_INVOKABLE void removeSource(int index);
    Q_INVOKABLE void addSourceConfiguration(
        const QString& name,
        const QString& subvolume,
        int localRetention,
        int remoteRetention
    );
    Q_INVOKABLE void updateSourceConfiguration(
        int index,
        const QString& name,
        int localRetention,
        int remoteRetention
    );
    Q_INVOKABLE void removeSourceConfiguration(int index);
    Q_INVOKABLE void updateProfileSettings(const QString& name, bool dailyLimit, bool autoEject);
    Q_INVOKABLE void validate();
    Q_INVOKABLE void save();
    Q_INVOKABLE void duplicateAs(const QString& profileId);
    Q_INVOKABLE void deleteProfile();

  signals:
    void draftChanged();
    void stateChanged();
    void conflictDetected();
    void profileSaved(const QString& profileId);
    void profileDeleted(const QString& profileId);

  private:
    enum class RequestKind { LoadDetails,
                             LoadEditing,
                             Validate,
                             Save,
                             Duplicate,
                             Delete };
    void request(RequestKind kind, const QString& method, const QVariantList& arguments);
    bool applyEnvelope(const QString& payload, bool replaceDraft);
    void updateDirty();
    void setError(const QString& code, const QString& message);
    void setBusy(bool value);
    QJsonObject object(const char* key) const;
    void setObject(const char* key, const QJsonObject& value);
    QString nextSourceId(const QString& name, const QString& subvolume) const;

    QDBusConnection bus_;
    QJsonObject draft_;
    QJsonObject baseline_;
    QString generation_;
    QString fingerprint_;
    QString error_code_;
    QString error_message_;
    QString operation_message_;
    QString validation_preview_;
    bool loaded_ = false;
    bool new_draft_ = false;
    bool dirty_ = false;
    bool busy_ = false;
};

} // namespace btrfsbackup::kde::kcm
