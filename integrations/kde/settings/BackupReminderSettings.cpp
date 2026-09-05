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

void BackupReminderSettings::setEnabled(const bool enabled) {
    if (configuration_.enabled == enabled)
        return;
    configuration_.enabled = enabled;
    save();
    emit enabledChanged();
}

void BackupReminderSettings::setWarningDays(const int days) {
    const auto updated = normalized({configuration_.enabled, days, configuration_.critical_days});
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
    const auto updated = normalized({configuration_.enabled, configuration_.warning_days, days});
    if (updated.critical_days == configuration_.critical_days)
        return;
    configuration_ = updated;
    save();
    emit criticalDaysChanged();
}

BackupReminderConfiguration BackupReminderSettings::load() {
    const KConfigGroup group(open_config(), QLatin1String(config_group));
    return normalized({
        group.readEntry("Enabled", true),
        group.readEntry("WarningDays", 7),
        group.readEntry("CriticalDays", 14),
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
    config->sync();
}

} // namespace btrfsbackup::kde
