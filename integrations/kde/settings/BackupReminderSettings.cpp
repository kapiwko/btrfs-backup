// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupReminderSettings.hpp"

#include <KConfigGroup>
#include <KSharedConfig>

#include <algorithm>

namespace btrfsbackup::kde {

namespace {

constexpr auto config_file = "btrfs-backuprc";
constexpr auto config_group = "BackupReminders";
constexpr int minimum_days = 1;
constexpr int maximum_days = 3650;
constexpr int minimum_percent = 1;
constexpr int maximum_percent = 99;

KSharedConfig::Ptr open_config() {
    const auto config = KSharedConfig::openConfig(QLatin1String(config_file));
    config->reparseConfiguration();
    return config;
}

BackupReminderConfiguration normalized(BackupReminderConfiguration configuration) {
    configuration.warning_days = std::clamp(configuration.warning_days, minimum_days, maximum_days - 1);
    configuration.critical_days = std::clamp(
        configuration.critical_days,
        configuration.warning_days + 1,
        maximum_days
    );
    configuration.storage_critical_percent = std::clamp(
        configuration.storage_critical_percent,
        minimum_percent,
        maximum_percent - 1
    );
    configuration.storage_warning_percent = std::clamp(
        configuration.storage_warning_percent,
        configuration.storage_critical_percent + 1,
        maximum_percent
    );
    return configuration;
}

} // namespace

BackupReminderSettings::BackupReminderSettings(QObject* parent)
    : QObject(parent), configuration_(load()) {
}

bool BackupReminderSettings::enabled() const {
    return configuration_.enabled;
}

int BackupReminderSettings::warningDays() const {
    return configuration_.warning_days;
}

int BackupReminderSettings::criticalDays() const {
    return configuration_.critical_days;
}

bool BackupReminderSettings::storageEnabled() const {
    return configuration_.storage_enabled;
}

int BackupReminderSettings::storageWarningPercent() const {
    return configuration_.storage_warning_percent;
}

int BackupReminderSettings::storageCriticalPercent() const {
    return configuration_.storage_critical_percent;
}

void BackupReminderSettings::setEnabled(const bool enabled) {
    if (configuration_.enabled == enabled)
        return;
    configuration_.enabled = enabled;
    save();
    emit enabledChanged();
}

void BackupReminderSettings::setWarningDays(const int days) {
    auto updated = configuration_;
    updated.warning_days = days;
    updated = normalized(updated);
    if (updated.warning_days == configuration_.warning_days &&
        updated.critical_days == configuration_.critical_days) {
        return;
    }
    const bool critical_changed = updated.critical_days != configuration_.critical_days;
    configuration_ = updated;
    save();
    emit warningDaysChanged();
    if (critical_changed)
        emit criticalDaysChanged();
}

void BackupReminderSettings::setCriticalDays(const int days) {
    auto updated = configuration_;
    updated.critical_days = days;
    updated = normalized(updated);
    if (updated.critical_days == configuration_.critical_days)
        return;
    configuration_ = updated;
    save();
    emit criticalDaysChanged();
}

void BackupReminderSettings::setStorageEnabled(const bool enabled) {
    if (configuration_.storage_enabled == enabled)
        return;
    configuration_.storage_enabled = enabled;
    save();
    emit storageEnabledChanged();
}

void BackupReminderSettings::setStorageWarningPercent(const int percent) {
    auto updated = configuration_;
    updated.storage_warning_percent = percent;
    updated = normalized(updated);
    if (updated.storage_warning_percent == configuration_.storage_warning_percent)
        return;
    configuration_ = updated;
    save();
    emit storageWarningPercentChanged();
}

void BackupReminderSettings::setStorageCriticalPercent(const int percent) {
    auto updated = configuration_;
    updated.storage_critical_percent = percent;
    updated = normalized(updated);
    if (updated.storage_critical_percent == configuration_.storage_critical_percent &&
        updated.storage_warning_percent == configuration_.storage_warning_percent) {
        return;
    }
    const bool warning_changed = updated.storage_warning_percent != configuration_.storage_warning_percent;
    configuration_ = updated;
    save();
    emit storageCriticalPercentChanged();
    if (warning_changed)
        emit storageWarningPercentChanged();
}

BackupReminderConfiguration BackupReminderSettings::load() {
    const KConfigGroup group(open_config(), QLatin1String(config_group));
    return normalized({
        group.readEntry("Enabled", true),
        group.readEntry("WarningDays", 7),
        group.readEntry("CriticalDays", 14),
        group.readEntry("StorageEnabled", true),
        group.readEntry("StorageWarningPercent", 15),
        group.readEntry("StorageCriticalPercent", 5),
    });
}

QString BackupReminderSettings::configFileName() {
    return QLatin1String(config_file);
}

void BackupReminderSettings::save() const {
    const auto config = open_config();
    KConfigGroup group(config, QLatin1String(config_group));
    group.writeEntry("Enabled", configuration_.enabled, KConfigBase::Notify);
    group.writeEntry("WarningDays", configuration_.warning_days, KConfigBase::Notify);
    group.writeEntry("CriticalDays", configuration_.critical_days, KConfigBase::Notify);
    group.writeEntry("StorageEnabled", configuration_.storage_enabled, KConfigBase::Notify);
    group.writeEntry("StorageWarningPercent", configuration_.storage_warning_percent, KConfigBase::Notify);
    group.writeEntry("StorageCriticalPercent", configuration_.storage_critical_percent, KConfigBase::Notify);
    config->sync();
}

} // namespace btrfsbackup::kde
